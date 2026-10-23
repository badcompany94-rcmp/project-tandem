#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "eos_bridge.h"
#include "eos_local_http.h"
#include "eos_protocol.h"
#include "eos_ping.h"
#include "eos_send_queue.h"
#include "../gcn-recomp/runtime/net/input_mesh_socket.h"
#include <chrono>
#include <deque>
#include <thread>

namespace mkdd::eos {
namespace {
using Clock=std::chrono::steady_clock;
constexpr int input_delay_ticks=3;
constexpr int input_buffer_ms=input_delay_ticks*1000/60;
uint64_t milliseconds(){return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
bool blocked(){return WSAGetLastError()==WSAEWOULDBLOCK;}
void nonblocking(SOCKET s){u_long value=1;if(ioctlsocket(s,FIONBIO,&value))throw std::runtime_error("loopback-socket-failed");}
SOCKET listener(unsigned& port) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)throw std::runtime_error("loopback-socket-failed");
    BOOL exclusive=TRUE;setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(s,(sockaddr*)&address,sizeof(address)) || listen(s,8)){closesocket(s);throw std::runtime_error("loopback-listen-failed");}
    int length=sizeof(address);getsockname(s,(sockaddr*)&address,&length);port=ntohs(address.sin_port);nonblocking(s);return s;
}
void nodelay(SOCKET s){int one=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof(one));}
}
struct Bridge::Impl {
    Runtime& runtime;unsigned upstream;
    Reassembler receiver;
    PeerPings pings;uint64_t next_ping=0;
    struct Outgoing {std::string peer,socket;uint8_t channel;std::vector<uint8_t> data;Clock::time_point queued=Clock::now();};
    std::deque<Outgoing> outgoing;size_t queued_bytes=0;
    struct Member {std::string token;unsigned seat=0;std::string uid;};
    std::map<std::string,Member> members;
    std::set<std::string> pending_kicks;
    bool kicking=false,refreshing=false;uint64_t next_kick=0;
    uint32_t next_id=1,waiting_id=0;std::optional<Json> reply;
    std::string game_id,game_socket;std::set<std::string> peers;
    std::unique_ptr<net::mesh::LoopbackMesh> mesh;
    std::vector<std::string> mesh_users;
    uint64_t mesh_closed_at=0;bool mesh_close_sent=false;
    unsigned game_port=0;SOCKET game_listener=INVALID_SOCKET;
    struct Stream {SOCKET socket=INVALID_SOCKET;bool connecting=false,opened=false;std::string pending;Clock::time_point started=Clock::now(),retry=Clock::now();};
    std::map<std::string,Stream> streams;
    Clock::time_point next_cleanup=Clock::now();

    Impl(Runtime& r,unsigned p):runtime(r),upstream(p) {
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock startup failed");
        runtime.packet=[this](const auto& peer,const auto& socket,uint8_t channel,const auto& data){received(peer,socket,channel,data);};
        runtime.idle=[this]{pump();};
    }
    ~Impl(){runtime.packet={};runtime.idle={};clear_game();WSACleanup();}
    void clear_game() {
        mesh.reset();mesh_users.clear();mesh_closed_at=0;mesh_close_sent=false;
        pings.peers({});next_ping=0;
        if(game_listener!=INVALID_SOCKET){closesocket(game_listener);game_listener=INVALID_SOCKET;}
        for(auto& [peer,stream]:streams)if(stream.socket!=INVALID_SOCKET)closesocket(stream.socket);
        streams.clear();peers.clear();game_id.clear();game_socket.clear();
        runtime.game_peers({},{});
        for(auto i=outgoing.begin();i!=outgoing.end();) {
            if(i->channel==1){queued_bytes-=i->data.size();i=outgoing.erase(i);}else ++i;
        }
    }
    void enqueue(const std::string& peer,const std::string& socket,uint8_t channel,std::vector<uint8_t> data) {
        if(data.empty() || data.size()>1170 || queued_bytes+data.size()>524288)throw std::runtime_error("eos-send-queue-full");
        queued_bytes+=data.size();outgoing.push_back({peer,socket,channel,std::move(data)});
    }
    void control(const std::string& peer,uint8_t kind,uint32_t id,const Json& body) {
        for(auto& fragment:fragments(kind,id,body.dump()))enqueue(peer,runtime.control_socket(),0,std::move(fragment));
    }
    Json host_request(const std::string& peer,Json request) {
        try {
            if(!runtime.hosting())throw std::runtime_error("invalid-session");
            const std::string op=request.at("op").str();
            if(peer!=runtime.self() && op=="create")throw std::runtime_error("host-only");
            if(op!="create" && op!="join") {
                if(!members.count(peer) || request.at("token").str()!=members.at(peer).token)throw std::runtime_error("invalid-session");
            }
            if(refreshing && op!="state")throw std::runtime_error("room-busy");
            if(op=="join" && members.count(peer))throw std::runtime_error("already-in-room");
            if(op=="join" && pending_kicks.count(peer))throw std::runtime_error("invalid-session");
            if(op=="start")request.object["endpoint"]="127.0.0.1:28121";
            Json result;
            if(op=="refresh-code") {
                struct Guard{bool& value;Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(refreshing);
                auto proposal=request;proposal.object.erase("code");proposal.object["propose"]=true;
                const auto before=local_request(upstream,Json::dict({{"op","state"},{"token",request.at("token")}}));
                if(before.has("error"))return before;
                const auto previous=before.at("room").at("code").str();
                for(unsigned attempt=0;;++attempt) {
                    result=local_request(upstream,proposal);if(result.has("error"))return result;
                    const auto code=result.at("code").str();
                    try{runtime.refresh_invitation(code);proposal.object.erase("propose");proposal.object["code"]=code;break;}
                    catch(const std::exception& e){if(std::string(e.what())!="room-code-in-use" || attempt>=15)throw;}
                }
                result=local_request(upstream,proposal);
                if(result.has("error"))runtime.refresh_invitation(previous);
            }else result=local_request(upstream,request);
            if(result.has("error"))return result;
            if(result.has("token"))members[peer].token=result.at("token").str();
            if(result.has("room")) {
                const auto& room=result.at("room");
                std::set<std::string> present;
                for(const auto& member:room.at("members").list()) {
                    const auto uid=member.has("id")?member.at("id").str():std::string();
                    if(member.at("you").boolean()) {members[peer].uid=uid;members[peer].seat=(unsigned)member.at("seat").integer(0,15);}
                    present.insert(uid);
                    for(auto& [id,known]:members)if(!uid.empty() && known.uid==uid)known.seat=(unsigned)member.at("seat").integer(0,15);
                }
                if(op=="kick")for(auto i=members.begin();i!=members.end();) {
                    if(!i->second.uid.empty() && !present.count(i->second.uid)){pending_kicks.insert(i->first);i=members.erase(i);}else ++i;
                }
                if(room.at("state").str()=="running") {
                    Json users;users.kind=Json::Array;
                    for(const auto& kart:room.at("launch").at("roster").list()) {
                        const unsigned seat=(unsigned)kart.at("seat").integer(0,15);std::string found;
                        for(const auto& [id,member]:members)if(member.seat==seat) {
                            if(!found.empty())throw std::runtime_error("invalid-eos-roster");found=id;
                        }
                        if(found.empty())throw std::runtime_error("invalid-eos-roster");users.array.emplace_back(found);
                    }
                    result.object["connection"].object["eos_users"]=std::move(users);
                }
            }
            if(op=="leave")members.erase(peer);
            return result;
        }catch(const std::exception& e){return Json::dict({{"error",e.what()}});}
    }
    void received(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data) {
        if(channel==2 && ((!runtime.control_socket().empty() && socket==runtime.control_socket()) || (!game_socket.empty() && socket==game_socket))) {
            auto reply=pings.received(peer,data,milliseconds());
            if(!reply.empty())enqueue(peer,socket,2,std::move(reply));
        }else if(channel==0 && !runtime.control_socket().empty() && socket==runtime.control_socket()) {
            try {
                auto message=receiver.add(peer,data);if(!message)return;
                Json body=Json::parse(message->body);
                if(message->kind==1 && runtime.hosting())control(peer,2,message->id,host_request(peer,std::move(body)));
                else if(message->kind==2 && peer==runtime.owner() && message->id==waiting_id)reply=std::move(body);
            }catch(const std::exception&){fputs("Rejected an invalid EOS control packet\n",stderr);}
        }else if(channel==1 && !game_socket.empty() && socket==game_socket && peers.count(peer) && !data.empty()) {
            if(mesh) {
                auto found=std::find(mesh_users.begin(),mesh_users.end(),peer);
                if(found==mesh_users.end())throw std::runtime_error("Invalid authenticated mesh sender");
                if(data[0]==13 && data.size()==1){mesh->disconnect();return;}
                mesh->peer(unsigned(found-mesh_users.begin()),data);return;
            }
            if(data[0]==1 && data.size()==1 && runtime.hosting())streams[peer].opened=true;
            else if(data[0]==2 && data.size()>1 && data.size()<=1001) {
                auto& stream=streams[peer];
                if(stream.pending.size()+data.size()-1>262144)throw std::runtime_error("eos-stream-buffer-full");
                stream.pending.append((const char*)data.data()+1,data.size()-1);
            }else if(data[0]==3 && data.size()==1) {
                auto i=streams.find(peer);if(i!=streams.end()) {
                    if(i->second.socket!=INVALID_SOCKET)closesocket(i->second.socket);streams.erase(i);
                }
            }
        }
    }
    Json remote_request(const Json& request) {
        if(runtime.owner().empty() || waiting_id)throw std::runtime_error("invalid-session");
        waiting_id=next_id++;if(!next_id)next_id=1;reply.reset();
        control(runtime.owner(),1,waiting_id,request);
        const auto deadline=Clock::now()+std::chrono::seconds(25);
        while(!reply) {
            runtime.tick();
            if(runtime.owner().empty()){waiting_id=0;throw std::runtime_error("invalid-session");}
            if(Clock::now()>deadline){waiting_id=0;throw std::runtime_error("service-unavailable");}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Json result=std::move(*reply);reply.reset();waiting_id=0;return result;
    }
    void prepare(Json& response) {
        if(!response.has("room") || response.at("room").at("state").str()!="running")return;
        const auto& launch=response.at("room").at("launch");
        const std::string id=launch.at("id").str();
        if(id.size()!=32 || id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("invalid-eos-session");
        const auto& users=response.at("connection").at("eos_users").list();
        const auto& roster=launch.at("roster").list();if(users.size()!=roster.size() || users.size()<2 || users.size()>8)throw std::runtime_error("invalid-eos-roster");
        const unsigned slot=(unsigned)response.at("connection").at("slot").integer(0,users.size()-1);
        if(users[slot].str()!=runtime.self() || users[0].str()!=runtime.owner())throw std::runtime_error("invalid-eos-roster");
        std::set<std::string> everyone;
        for(const auto& user:users) {
            const std::string value=user.str();
            if(value.size()!=32 || value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos || !everyone.insert(value).second)throw std::runtime_error("invalid-eos-roster");
        }
        everyone.erase(runtime.self());
        if(game_id!=id) {
            clear_game();game_id=id;game_socket=id;
            const bool direct=getenv("TANDEM_EOS_MESH")!=nullptr || getenv("TANDEM_EOS_ROLLBACK")!=nullptr;
            peers=direct || runtime.hosting()?everyone:std::set<std::string>{runtime.owner()};
            runtime.game_peers(game_socket,peers);
            if(direct) {
                for(const auto& user:users)mesh_users.push_back(user.str());
                const auto ticket=response.at("connection").at("admission").str();
                if(ticket.size()!=32 || ticket.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("invalid-mesh-admission");
                net::Admission admission{};
                for(unsigned i=0;i<16;++i)admission[i]=uint8_t(std::stoul(ticket.substr(i*2,2),nullptr,16));
                mesh=std::make_unique<net::mesh::LoopbackMesh>(unsigned(users.size()),slot,admission,[this](unsigned recipient,const auto& data){
                    enqueue(mesh_users.at(recipient),game_socket,1,data);
                },getenv("TANDEM_EOS_ORDERED_INPUT")==nullptr);
                fprintf(stderr,"[input-transport] %s\n",getenv("TANDEM_EOS_ORDERED_INPUT")?"reliable ordered diagnostic fallback":"redundant datagrams with ordered input recovery");
                game_port=mesh->port();
            }else {
                game_listener=listener(game_port);
                if(runtime.hosting()){closesocket(game_listener);game_listener=INVALID_SOCKET;}
            }
            puts("EOS game bridge prepared; game sockets stay on loopback");
        }
        if(mesh)response.object["connection"].object["mesh"]=true;
        if(mesh && getenv("TANDEM_EOS_ROLLBACK"))response.object["connection"].object["prediction"]=true;
        response.object["connection"].object["endpoint"]="127.0.0.1:"+std::to_string(game_port);
        // Three simulation ticks give a 50 ms budget at 60 Hz. Keep this
        // identical for every peer; the native handshake checks agreement.
        response.object["connection"].object["input_delay"]=input_delay_ticks;
    }
    Json request(const Json& request) {
        try {
            const std::string op=request.at("op").str();Json response;
            if(op=="create") {
                if(!runtime.owner().empty())throw std::runtime_error("already-in-room");
                for(unsigned attempt=0;;++attempt) {
                    response=local_request(upstream,request);if(response.has("error"))return response;
                    try{runtime.create(response.at("room").at("code").str(),request.at("compatibility").str());break;}
                    catch(const std::exception& e){local_request(upstream,Json::dict({{"op","leave"},{"token",response.at("token")}}));
                        if(std::string(e.what())!="room-code-in-use" || attempt>=15)throw;}
                }
                members[runtime.self()]={response.at("token").str(),0};
                for(const auto& member:response.at("room").at("members").list())if(member.at("you").boolean() && member.has("id"))members[runtime.self()].uid=member.at("id").str();
            }else if(op=="join") {
                std::string code=request.at("code").str();
                if(!((code.size()==4 && code.find_first_not_of("0123456789")==std::string::npos) || (code.size()==8 && code.find_first_not_of("23456789ABCDEFGHJKLMNPQRSTUVWXYZ")==std::string::npos)))throw std::runtime_error("invalid-code");
                runtime.join(code,request.at("compatibility").str());
                try{response=remote_request(request);}catch(...){runtime.leave();throw;}
                if(response.has("error")){runtime.leave();return response;}
            }else response=runtime.hosting()?host_request(runtime.self(),request):remote_request(request);
            if(!response.has("error")) {
                prepare(response);
                if(op=="leave"){runtime.leave();clear_game();members.clear();}
                else if(op=="finish" || op=="abort")clear_game();
            }
            return response;
        }catch(const std::exception& e){return Json::dict({{"error",e.what()}});}
    }
    void pump() {
        const auto now=milliseconds();
        if(runtime.owner().empty()) {
            clear_game();members.clear();pending_kicks.clear();outgoing.clear();queued_bytes=0;
            return;
        }
        if(!kicking && !pending_kicks.empty() && now>=next_kick) {
            next_kick=now+2000;
            kicking=true;const auto peer=*pending_kicks.begin();
            try{runtime.kick(peer);pending_kicks.erase(peer);}catch(const std::exception&){
                if(!runtime.members().count(peer))pending_kicks.erase(peer);
            }
            kicking=false;
        }
        if(mesh) {
            mesh->pump(now);
            if(mesh->closed() && !mesh_closed_at)mesh_closed_at=now;
            // Let already queued inputs reach peers before signaling closure.
            // A remote closure closes only the game socket; the lobby survives.
            if(mesh_closed_at && !mesh_close_sent && now-mesh_closed_at>=1000) {
                for(const auto& peer:peers)enqueue(peer,game_socket,1,{13});
                mesh_close_sent=true;
            }
        }
        if(now>=next_ping) {
            next_ping=now+100;
            auto current=!game_socket.empty()?peers:(runtime.hosting()?runtime.members():std::set<std::string>{});
            if(game_socket.empty() && !runtime.hosting() && !runtime.owner().empty())current.insert(runtime.owner());
            current.erase(runtime.self());pings.peers(current);
            for(const auto& peer:current) {
                auto probe=pings.probe(peer,now);
                if(!probe.empty())enqueue(peer,game_socket.empty()?runtime.control_socket():game_socket,2,std::move(probe));
            }
        }
        if(Clock::now()>=next_cleanup) {
            next_cleanup=Clock::now()+std::chrono::seconds(1);
            if(runtime.hosting()) {
                const auto current=runtime.members();
                for(auto i=members.begin();i!=members.end();) {
                    if(i->first!=runtime.self() && !current.count(i->first)) {
                        local_request(upstream,Json::dict({{"op","leave"},{"token",i->second.token}}));
                        i=members.erase(i);clear_game();
                    }else ++i;
                }
            }
        }
        if(game_listener!=INVALID_SOCKET) {
            SOCKET s=accept(game_listener,nullptr,nullptr);
            if(s!=INVALID_SOCKET) {
                if(streams.count(runtime.owner()))closesocket(s);
                else {nonblocking(s);nodelay(s);streams[runtime.owner()].socket=s;streams[runtime.owner()].opened=true;enqueue(runtime.owner(),game_socket,1,{1});}
            }
        }
        for(auto it=streams.begin();it!=streams.end();) {
            auto& stream=it->second;bool closed=false;
            if(runtime.hosting() && stream.opened && stream.socket==INVALID_SOCKET && Clock::now()>=stream.retry) {
                if(Clock::now()-stream.started>std::chrono::seconds(45))throw std::runtime_error("Native host did not open its loopback game socket");
                stream.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);nonblocking(stream.socket);nodelay(stream.socket);
                sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)game_port);
                const int result=connect(stream.socket,(sockaddr*)&address,sizeof(address));
                stream.connecting=result!=0;
                if(result && WSAGetLastError()!=WSAEWOULDBLOCK){closesocket(stream.socket);stream.socket=INVALID_SOCKET;stream.retry=Clock::now()+std::chrono::milliseconds(20);}
            }
            if(stream.socket!=INVALID_SOCKET && stream.connecting) {
                fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(stream.socket,&write);FD_SET(stream.socket,&error);timeval timeout{};
                if(select(0,nullptr,&write,&error,&timeout)>0) {
                    int code=0,length=sizeof(code);getsockopt(stream.socket,SOL_SOCKET,SO_ERROR,(char*)&code,&length);
                    if(code || FD_ISSET(stream.socket,&error)){closesocket(stream.socket);stream.socket=INVALID_SOCKET;stream.retry=Clock::now()+std::chrono::milliseconds(20);}
                    else stream.connecting=false;
                }
            }
            if(stream.socket!=INVALID_SOCKET && !stream.connecting) {
                if(!stream.pending.empty()) {
                    int n=send(stream.socket,stream.pending.data(),(int)stream.pending.size(),0);
                    if(n>0)stream.pending.erase(0,n);else if(!blocked())closed=true;
                }
                char bytes[1000];int n=recv(stream.socket,bytes,sizeof(bytes),0);
                if(n>0) {std::vector<uint8_t> packet={2};packet.insert(packet.end(),bytes,bytes+n);enqueue(it->first,game_socket,1,std::move(packet));}
                else if(!n || !blocked())closed=true;
            }
            if(closed){enqueue(it->first,game_socket,1,{3});if(stream.socket!=INVALID_SOCKET)closesocket(stream.socket);it=streams.erase(it);}else ++it;
        }
        // Include input just read from the native socket in this pump. Flushing
        // before the reads added an entire polling interval to every input.
        drain_send_queue(outgoing,queued_bytes,Clock::now(),std::chrono::seconds(30),
            [this](const auto& packet){return runtime.authorized(packet.peer,packet.socket);},
            [this](const auto& packet){return runtime.send(packet.peer,packet.socket,packet.channel,packet.data);});
    }
};
Bridge::Bridge(Runtime& runtime,unsigned port):impl_(std::make_unique<Impl>(runtime,port)){}
Bridge::~Bridge()=default;
Json Bridge::request(const Json& request){return impl_->request(request);}
void Bridge::pump(){impl_->pump();}
Json Bridge::statistics(uint64_t sequence)const {
    const auto values=impl_->pings.summary(milliseconds());
    return Json::dict({{"format",1},{"sequence",(int64_t)sequence},{"rtt_ms",values.rtt_ms},
        {"rtt_age_ms",(int64_t)values.age_ms},{"peers",(int)values.peers},{"buffer_ms",input_buffer_ms}});
}
}
