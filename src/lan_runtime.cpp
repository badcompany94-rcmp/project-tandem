#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "lan_runtime.h"
#include "online_json.h"
#include "eos_poll_wait.h"
#include <algorithm>
#include <chrono>
#include <map>

namespace mkdd::eos {
namespace {
using online::Json;
using Clock=std::chrono::steady_clock;
bool blocked(){return WSAGetLastError()==WSAEWOULDBLOCK;}
void nonblocking(SOCKET s){u_long one=1;if(ioctlsocket(s,FIONBIO,&one))throw std::runtime_error("lan-socket-failed");}
void close_socket(SOCKET& s){if(s!=INVALID_SOCKET){closesocket(s);s=INVALID_SOCKET;}}
std::string random_id(){
    unsigned char bytes[16];
    if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG))throw std::runtime_error("lan-random-failed");
    std::string out;for(auto b:bytes){out.push_back("0123456789abcdef"[b>>4]);out.push_back("0123456789abcdef"[b&15]);}return out;
}
std::string hex(const std::vector<uint8_t>& bytes){
    std::string out;for(auto b:bytes){out.push_back("0123456789abcdef"[b>>4]);out.push_back("0123456789abcdef"[b&15]);}return out;
}
std::vector<uint8_t> unhex(const std::string& text){
    if(text.empty() || text.size()>2340 || text.size()%2 || text.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("invalid-lan-packet");
    std::vector<uint8_t> out;for(size_t i=0;i<text.size();i+=2)out.push_back(uint8_t(std::stoul(text.substr(i,2),nullptr,16)));return out;
}
std::string id(const Json& value){
    auto s=value.str();if(s.size()!=32 || s.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("invalid-lan-peer");return s;
}
bool local_ip(uint32_t network){
    auto n=ntohl(network);return (n>>24)==10 || (n>>24)==127 || (n>>20)==0xac1 || (n>>16)==0xc0a8 || (n>>16)==0xa9fe;
}
sockaddr_in address(const std::string& ip,unsigned port){
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(u_short(port));
    if(!port || port>65535 || inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1 || !local_ip(a.sin_addr.s_addr))throw std::runtime_error("invalid-lan-address");return a;
}
std::string ip(const sockaddr_in& a){char text[INET_ADDRSTRLEN];inet_ntop(AF_INET,&a.sin_addr,text,sizeof(text));return text;}
SOCKET listener(unsigned port,bool udp){
    SOCKET s=socket(AF_INET,udp?SOCK_DGRAM:SOCK_STREAM,udp?IPPROTO_UDP:IPPROTO_TCP);
    if(s==INVALID_SOCKET)throw std::runtime_error("lan-socket-failed");
    BOOL one=TRUE;setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&one,sizeof(one));
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(u_short(port));a.sin_addr.s_addr=INADDR_ANY;
    if(bind(s,(sockaddr*)&a,sizeof(a)) || (!udp && listen(s,8))){fprintf(stderr,"[lan-listen] port=%u udp=%d error=%d\n",port,int(udp),WSAGetLastError());closesocket(s);throw std::runtime_error("lan-port-in-use");}
    nonblocking(s);return s;
}
unsigned port_of(SOCKET s){sockaddr_in a{};int n=sizeof(a);getsockname(s,(sockaddr*)&a,&n);return ntohs(a.sin_port);}
}

struct LanRuntime::Impl {
    LanRuntime& api;std::string target;unsigned port;
    SOCKET tcp=INVALID_SOCKET,discovery=INVALID_SOCKET,udp=INVALID_SOCKET;
    std::string self=random_id(),owner,control,game,code,build,cookie=random_id();
    std::set<std::string> game_members;
    struct Peer {sockaddr_in endpoint{};std::string cookie;};
    std::map<std::string,Peer> peers;
    struct Stream {SOCKET socket; sockaddr_in remote{};std::string peer,input,output;Clock::time_point started=Clock::now();bool closing=false;};
    std::map<SOCKET,Stream> streams;
    std::string join_error;
    bool welcomed=false,in_tick=false;
    unsigned traced_send=0,traced_receive=0;

    Impl(LanRuntime& a,std::string host,unsigned p):api(a),target(std::move(host)),port(p){
        if(!port || port>65535)throw std::runtime_error("invalid-lan-port");
        if(!target.empty())address(target,port);
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("lan-socket-failed");
        try{udp=listener(0,true);}catch(...){WSACleanup();throw;}
    }
    ~Impl(){try{leave();}catch(...){}close_socket(udp);WSACleanup();}
    bool hosting()const{return !owner.empty() && owner==self;}
    void leave(){
        close_socket(tcp);close_socket(discovery);
        for(auto& [s,c]:streams)closesocket(s);streams.clear();
        owner.clear();control.clear();game.clear();code.clear();build.clear();peers.clear();game_members.clear();welcomed=false;
        // Rotate the input credential between rooms; stale datagrams cannot join a new session.
        cookie=random_id();
    }
    void queue(Stream& c,const Json& message){
        auto body=message.dump()+"\n";
        if(body.size()>8192 || c.output.size()+body.size()>262144)throw std::runtime_error("lan-send-queue-full");
        c.output+=body;
    }
    Json roster(Stream& recipient){
        Json list;list.kind=Json::Array;
        sockaddr_in own{};int n=sizeof(own);getsockname(recipient.socket,(sockaddr*)&own,&n);
        list.array.push_back(Json::dict({{"id",self},{"ip",ip(own)},{"port",(int)port_of(udp)},{"cookie",cookie}}));
        for(const auto& [peer,value]:peers)if(peer!=self){
            auto endpoint=value.endpoint;
            // A second game on the host may join through loopback. Other PCs
            // must reach that helper through the host's LAN interface.
            if((ntohl(endpoint.sin_addr.s_addr)>>24)==127 && (ntohl(recipient.remote.sin_addr.s_addr)>>24)!=127)endpoint.sin_addr=own.sin_addr;
            list.array.push_back(Json::dict({{"id",peer},{"ip",ip(endpoint)},{"port",(int)ntohs(endpoint.sin_port)},{"cookie",value.cookie}}));
        }
        return Json::dict({{"op","roster"},{"members",list}});
    }
    void broadcast(){for(auto& [s,c]:streams)if(!c.peer.empty() && !c.closing)queue(c,roster(c));}
    void drop(SOCKET socket){
        auto found=streams.find(socket);if(found==streams.end())return;
        const auto peer=found->second.peer;closesocket(socket);streams.erase(found);
        if(hosting()){peers.erase(peer);broadcast();}
        else {for(auto& [s,c]:streams)closesocket(s);streams.clear();owner.clear();control.clear();game.clear();peers.clear();game_members.clear();}
    }
    bool allowed(const std::string& peer,const std::string& socket)const{
        return !owner.empty() && peer!=self && peers.count(peer) &&
            ((!control.empty() && socket==control) || (!game.empty() && socket==game && game_members.count(peer)));
    }
    void deliver(const std::string& from,const Json& m){
        const auto socket=m.at("socket").str();const auto channel=uint8_t(m.at("channel").integer(0,2));
        if(!allowed(from,socket)){if(getenv("TANDEM_LAN_TRACE"))fputs("[lan-deliver] unauthorized socket\n",stderr);return;}
        auto bytes=unhex(m.at("data").str());if(api.packet)api.packet(from,socket,channel,bytes);
    }
    void message(Stream& c,const Json& m){
        const auto op=m.at("op").str();
        if(hosting() && c.peer.empty()){
            if(op!="hello" || m.at("protocol").integer(1,1)!=1 || m.at("code").str()!=code)throw std::runtime_error("room-not-found");
            if(m.at("build").str()!=build){queue(c,Json::dict({{"op","error"},{"error","incompatible-build"}}));c.closing=true;return;}
            if(peers.size()>=8){queue(c,Json::dict({{"op","error"},{"error","room-full"}}));c.closing=true;return;}
            c.peer=random_id();Peer peer;peer.endpoint=c.remote;peer.endpoint.sin_port=htons(u_short(m.at("port").integer(1,65535)));peer.cookie=id(m.at("cookie"));peers.emplace(c.peer,peer);
            queue(c,Json::dict({{"op","welcome"},{"self",c.peer},{"owner",self},{"control",control}}));broadcast();return;
        }
        if(!hosting() && op=="error"){join_error=m.at("error").str();return;}
        if(!hosting() && op=="welcome"){
            if(!owner.empty())throw std::runtime_error("invalid-lan-welcome");
            self=id(m.at("self"));owner=id(m.at("owner"));control=id(m.at("control"));c.peer=owner;return;
        }
        if(!hosting() && op=="roster"){
            if(owner.empty())throw std::runtime_error("invalid-lan-roster");
            const auto& values=m.at("members").list();if(values.empty() || values.size()>8)throw std::runtime_error("invalid-lan-roster");
            std::map<std::string,Peer> next;
            for(const auto& value:values){
                auto who=id(value.at("id"));Peer peer;peer.endpoint=address(value.at("ip").str(),unsigned(value.at("port").integer(1,65535)));peer.cookie=id(value.at("cookie"));
                if(!next.emplace(who,peer).second)throw std::runtime_error("invalid-lan-roster");
            }
            if(!next.count(owner) || !next.count(self) || next.at(self).cookie!=cookie)throw std::runtime_error("invalid-lan-roster");
            peers=std::move(next);welcomed=true;return;
        }
        if(op!="packet" || c.peer.empty())throw std::runtime_error("invalid-lan-packet");
        const auto to=id(m.at("to"));
        if(hosting()){
            if(to==self){deliver(c.peer,m);return;}
            // Reliable mesh handshake/control recovery is routed through the host.
            // Racing inputs take the direct UDP path instead.
            auto forwarded=m;forwarded.object["from"]=c.peer;
            for(auto& [s,remote]:streams)if(remote.peer==to && !remote.closing){queue(remote,forwarded);return;}
        }else {
            if(to!=self)throw std::runtime_error("invalid-lan-recipient");deliver(id(m.at("from")),m);
        }
    }
    void discovery_tick(){
        if(discovery==INVALID_SOCKET)return;
        for(unsigned i=0;i<32;++i){
            char data[512];sockaddr_in from{};int n=sizeof(from);int size=recvfrom(discovery,data,sizeof(data),0,(sockaddr*)&from,&n);
            if(size<=0)break;if(!local_ip(from.sin_addr.s_addr))continue;
            try{auto q=Json::parse(std::string(data,size));
                if(q.at("service").str()!="tandem-lan-1" || q.at("code").str()!=code)continue;
                auto body=Json::dict({{"service","tandem-lan-1"},{"code",code},{"build",build},{"port",(int)port}}).dump();
                sendto(discovery,body.data(),int(body.size()),0,(sockaddr*)&from,sizeof(from));
            }catch(const std::exception&){}
        }
    }
    void udp_tick(){
        for(unsigned i=0;i<256;++i){
            char data[1500];sockaddr_in from{};int n=sizeof(from);int size=recvfrom(udp,data,sizeof(data),0,(sockaddr*)&from,&n);
            if(size<=0)break;
            try{
                auto m=Json::parse(std::string(data,size));auto sender=id(m.at("from"));
                if(getenv("TANDEM_LAN_TRACE") && traced_receive++<2)fprintf(stderr,"[lan-udp] received %d bytes on %u\n",size,port_of(udp));
                auto found=peers.find(sender);if(found==peers.end())continue;
                if(!local_ip(from.sin_addr.s_addr) || from.sin_port!=found->second.endpoint.sin_port || m.at("cookie").str()!=found->second.cookie || id(m.at("to"))!=self)continue;
                // Let authenticated traffic select its actual local interface.
                // The TCP route to the host and direct peer route can differ.
                found->second.endpoint.sin_addr=from.sin_addr;
                auto channel=m.at("channel").integer(0,2);auto bytes=unhex(m.at("data").str());
                if(channel!=1 || bytes[0]!=14)continue;
                deliver(sender,m);
            }catch(const std::exception& e){if(getenv("TANDEM_LAN_TRACE"))fprintf(stderr,"[lan-udp-reject] %s\n",e.what());}
        }
    }
    void tick(){
        if(in_tick)return;
        struct Guard{bool& b;Guard(bool& x):b(x){b=true;}~Guard(){b=false;}}guard(in_tick);
        discovery_tick();
        if(tcp!=INVALID_SOCKET)for(unsigned i=0;i<8;++i){
            sockaddr_in from{};int n=sizeof(from);SOCKET s=accept(tcp,(sockaddr*)&from,&n);if(s==INVALID_SOCKET)break;
            if(streams.size()>=15 || !local_ip(from.sin_addr.s_addr)){closesocket(s);continue;}
            nonblocking(s);int one=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char*)&one,sizeof(one));streams.emplace(s,Stream{s,from});
        }
        std::vector<SOCKET> current;for(auto& [s,c]:streams)current.push_back(s);
        for(auto s:current){
            auto it=streams.find(s);if(it==streams.end())continue;auto& c=it->second;bool closed=false;
            if(c.peer.empty() && Clock::now()-c.started>std::chrono::seconds(5))closed=true;
            char bytes[8192];int n=recv(s,bytes,sizeof(bytes),0);
            if(n>0)c.input.append(bytes,n);else if(!n || !blocked())closed=true;
            if(c.input.size()>65536)closed=true;
            if(!closed && !c.closing)try{
                unsigned messages=0;
                while(messages++<64){auto end=c.input.find('\n');if(end==std::string::npos)break;if(end>8192)throw std::runtime_error("invalid-lan-size");
                    auto body=c.input.substr(0,end);c.input.erase(0,end+1);message(c,Json::parse(body));if(c.closing)break;}
                if(c.input.size()>8192)throw std::runtime_error("invalid-lan-size");
            }catch(const std::exception&){closed=true;}
            if(!closed && !c.output.empty()){
                int sent=::send(s,c.output.data(),int(c.output.size()),0);if(sent>0)c.output.erase(0,sent);else if(!blocked())closed=true;
            }
            if(c.closing && c.output.empty())closed=true;
            if(closed){drop(s);if(!hosting())break;}
        }
        udp_tick();if(api.idle)api.idle();
    }
    std::string discover(const std::string& wanted){
        SOCKET query=listener(0,true);BOOL one=TRUE;setsockopt(query,SOL_SOCKET,SO_BROADCAST,(char*)&one,sizeof(one));
        auto body=Json::dict({{"service","tandem-lan-1"},{"code",wanted}}).dump();
        const auto deadline=Clock::now()+std::chrono::seconds(2);auto next=Clock::time_point{};PollWait wait;
        std::string found;
        while(Clock::now()<deadline && found.empty()){
            if(Clock::now()>=next){
                next=Clock::now()+std::chrono::milliseconds(250);
                for(auto addr:{"255.255.255.255","127.0.0.1"}){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(u_short(port));inet_pton(AF_INET,addr,&a.sin_addr);sendto(query,body.data(),int(body.size()),0,(sockaddr*)&a,sizeof(a));}
            }
            char data[512];sockaddr_in from{};int n=sizeof(from);int size=recvfrom(query,data,sizeof(data),0,(sockaddr*)&from,&n);
            if(size>0 && local_ip(from.sin_addr.s_addr))try{
                auto q=Json::parse(std::string(data,size));if(q.at("service").str()=="tandem-lan-1" && q.at("code").str()==wanted && q.at("port").integer(1,65535)==port)found=ip(from);
            }catch(const std::exception&){}
            tick();wait.wait();
        }
        closesocket(query);if(found.empty())throw std::runtime_error("lan-room-not-found");return found;
    }
};

LanRuntime::LanRuntime(std::string host,unsigned port):impl_(std::make_unique<Impl>(*this,std::move(host),port)){}
LanRuntime::~LanRuntime()=default;
void LanRuntime::tick(){impl_->tick();}
void LanRuntime::create(const std::string& code,const std::string& build){
    auto& p=*impl_;if(!p.owner.empty())throw std::runtime_error("already-in-room");
    try{p.tcp=listener(p.port,false);p.discovery=listener(p.port,true);}catch(...){p.leave();throw;}
    p.code=code;p.build=build;p.owner=p.self;p.control=random_id();p.peers[p.self]={};
    puts("LAN room ready: direct local UDP inputs, no Internet login");
}
void LanRuntime::join(const std::string& code,const std::string& build){
    auto& p=*impl_;if(!p.owner.empty())throw std::runtime_error("already-in-room");
    auto host=p.target.empty()?p.discover(code):p.target;auto endpoint=address(host,p.port);
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)throw std::runtime_error("lan-socket-failed");
    nonblocking(s);int one=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char*)&one,sizeof(one));
    if(connect(s,(sockaddr*)&endpoint,sizeof(endpoint)) && !blocked()){closesocket(s);throw std::runtime_error("lan-connect-failed");}
    const auto deadline=Clock::now()+std::chrono::seconds(4);PollWait wait;
    for(;;){fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(s,&write);FD_SET(s,&error);timeval timeout{};
        if(select(0,nullptr,&write,&error,&timeout)>0){int code=0,n=sizeof(code);getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&code,&n);if(code || FD_ISSET(s,&error)){closesocket(s);throw std::runtime_error("lan-connect-failed");}break;}
        if(Clock::now()>deadline){closesocket(s);throw std::runtime_error("lan-connect-failed");}wait.wait();}
    p.join_error.clear();p.welcomed=false;p.streams.emplace(s,Impl::Stream{s,endpoint});
    p.queue(p.streams.at(s),Json::dict({{"op","hello"},{"protocol",1},{"code",code},{"build",build},{"port",(int)port_of(p.udp)},{"cookie",p.cookie}}));
    while(!p.welcomed && p.join_error.empty() && !p.streams.empty() && Clock::now()<deadline){tick();wait.wait();}
    if(!p.welcomed){const auto error=p.join_error.empty()?"lan-connect-failed":p.join_error;p.leave();throw std::runtime_error(error);}
}
void LanRuntime::leave(){impl_->leave();}
void LanRuntime::kick(const std::string& peer){auto& p=*impl_;if(!p.hosting())throw std::runtime_error("host-only");for(auto& [s,c]:p.streams)if(c.peer==peer){p.drop(s);return;}}
void LanRuntime::refresh_invitation(const std::string& code){if(!hosting())throw std::runtime_error("host-only");impl_->code=code;}
std::set<std::string> LanRuntime::members()const{std::set<std::string> out;for(auto& [id,p]:impl_->peers)out.insert(id);return out;}
bool LanRuntime::send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data){
    auto& p=*impl_;if(!p.allowed(peer,socket) || data.empty() || data.size()>1170 || channel>2)return false;
    auto message=Json::dict({{"op","packet"},{"to",peer},{"from",p.self},{"socket",socket},{"channel",(int)channel},{"data",hex(data)}});
    if(channel==1 && data[0]==14){
        message.object["cookie"]=p.cookie;auto bytes=message.dump();if(bytes.size()>1400)return false;
        auto& destination=p.peers.at(peer).endpoint;
        int result=sendto(p.udp,bytes.data(),int(bytes.size()),0,(sockaddr*)&destination,sizeof(destination));
        if(getenv("TANDEM_LAN_TRACE") && p.traced_send++<2)fprintf(stderr,"[lan-udp] sent %d bytes to %s:%u\n",result,ip(destination).c_str(),unsigned(ntohs(destination.sin_port)));
        return result==int(bytes.size());
    }
    for(auto& [s,c]:p.streams)if(c.peer==(p.hosting()?peer:p.owner) && !c.closing){p.queue(c,message);return true;}
    return false;
}
void LanRuntime::game_peers(std::string socket,std::set<std::string> peers){
    peers.erase(self());if(peers.size()>7 || (socket.empty() && !peers.empty()))throw std::runtime_error("invalid-lan-roster");
    for(const auto& peer:peers)if(!impl_->peers.count(peer))throw std::runtime_error("invalid-lan-roster");
    impl_->game=std::move(socket);impl_->game_members=std::move(peers);
}
const std::string& LanRuntime::self()const{return impl_->self;}
const std::string& LanRuntime::owner()const{return impl_->owner;}
bool LanRuntime::hosting()const{return impl_->hosting();}
const std::string& LanRuntime::control_socket()const{return impl_->control;}
bool LanRuntime::authorized(const std::string& peer,const std::string& socket)const{return impl_->allowed(peer,socket);}
}
