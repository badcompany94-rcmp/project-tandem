#include "peer_link.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket no_socket = INVALID_SOCKET;
static void close_socket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
using Socket = int;
static constexpr Socket no_socket = -1;
static void close_socket(Socket s) { close(s); }
#endif

namespace net {
namespace {
using Packet = std::array<uint8_t, 64>;
enum Kind { Hello=1, Welcome=2, Input=3, Bundle=4 };
void put(Packet& p, unsigned at, uint64_t value, unsigned n) {
    for (unsigned i=0; i<n; ++i) { p[at+n-1-i]=(uint8_t)value; value >>= 8; }
}
uint64_t get(const Packet& p, unsigned at, unsigned n) {
    uint64_t value=0;
    for (unsigned i=0; i<n; ++i) value=(value<<8)|p[at+i];
    return value;
}
Packet packet(Kind kind, uint64_t tick=0, uint64_t hash=0) {
    Packet p{};
    memcpy(p.data(), "GCI1", 4);
    put(p,4,2,2); put(p,6,kind,2); put(p,8,tick,8); put(p,16,hash,8);
    return p;
}
bool valid(const Packet& p, Kind kind) {
    return memcmp(p.data(),"GCI1",4)==0 && get(p,4,2)==2 && get(p,6,2)==kind;
}
bool ready(Socket s, bool write, std::chrono::steady_clock::time_point until) {
    const auto left=std::chrono::duration_cast<std::chrono::microseconds>(until-std::chrono::steady_clock::now()).count();
    if (left<=0) return false;
    fd_set fds; FD_ZERO(&fds); FD_SET(s,&fds);
    timeval timeout{(long)(left/1000000), (long)(left%1000000)};
    return select((int)s+1,write?nullptr:&fds,write?&fds:nullptr,nullptr,&timeout)>0;
}
bool transfer(Socket s, Packet& p, bool send_packet) {
    size_t done=0;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (done<p.size()) {
        if (!ready(s,send_packet,until)) return false;
        int flags=0;
#ifdef MSG_NOSIGNAL
        flags=MSG_NOSIGNAL;
#endif
        const int n=send_packet ? send(s,(const char*)p.data()+done,(int)(p.size()-done),flags)
                                : recv(s,(char*)p.data()+done,(int)(p.size()-done),0);
        if (n<=0) return false;
        done+=(size_t)n;
    }
    return true;
}
bool address(const std::string& endpoint, sockaddr_in& out) {
    const auto colon=endpoint.rfind(':');
    if (colon==std::string::npos) return false;
    const auto port_text=endpoint.substr(colon+1);
    if (port_text.empty() || port_text.find_first_not_of("0123456789")!=std::string::npos || port_text.size()>5) return false;
    const unsigned port=(unsigned)std::stoul(port_text);
    out={}; out.sin_family=AF_INET; out.sin_port=htons((uint16_t)port);
    return port>0 && port<=65535 && inet_pton(AF_INET,endpoint.substr(0,colon).c_str(),&out.sin_addr)==1;
}
void tune(Socket s) {
    int one=1;
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof(one));
}
bool connect_bounded(Socket s, sockaddr_in& addr) {
#ifdef _WIN32
    u_long mode=1;
    if (ioctlsocket(s,FIONBIO,&mode)!=0) return false;
#else
    const int flags=fcntl(s,F_GETFL,0);
    if (flags<0 || fcntl(s,F_SETFL,flags|O_NONBLOCK)!=0) return false;
#endif
    const int result=connect(s,(sockaddr*)&addr,sizeof(addr));
    bool ok=result==0;
    if (!ok && ready(s,true,std::chrono::steady_clock::now()+std::chrono::seconds(10))) {
        int error=0;
#ifdef _WIN32
        int size=sizeof(error);
#else
        socklen_t size=sizeof(error);
#endif
        ok=getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&size)==0 && error==0;
    }
#ifdef _WIN32
    mode=0; ioctlsocket(s,FIONBIO,&mode);
#else
    fcntl(s,F_SETFL,flags);
#endif
    return ok;
}
}
struct PeerLink::Impl {
    std::vector<Socket> sockets;
    unsigned slot=0, count=0;
    uint64_t next=1;
    bool host=false;
    std::atomic<bool> failed{false};
#ifdef _WIN32
    bool initialized=false;
#endif
    std::string error;
    unsigned delay=0;
    unsigned prediction=0;
    bool mesh=false;
    bool predicting=false;
    bool draining=false;
    unsigned drain_remaining=0;
    uint64_t receive_next=1;
    uint64_t main_next=1;
    struct Job {uint64_t tick,hash;std::array<uint8_t,2> local;unsigned delay=0,lead=0;};
    std::deque<Job> jobs;
    std::deque<Job> inflight;
    struct Received {Inputs inputs;Pacing pacing;};
    std::map<uint64_t,Received> bundles;
    std::mutex mutex;std::condition_variable changed;std::thread worker,receiver;
    bool stopping=false,worker_failed=false;
    std::string async_error;
    bool fail(const char* message) { error=message; failed=true; return false; }
    Impl() {
#ifdef _WIN32
        WSADATA data{};
        initialized=WSAStartup(MAKEWORD(2,2),&data)==0;
        if (!initialized) fail("Socket initialization failed");
#endif
    }
    void shutdown_sockets() {
        for(Socket s:sockets) {
#ifdef _WIN32
            shutdown(s,SD_BOTH);
#else
            shutdown(s,SHUT_RDWR);
#endif
        }
    }
    // Called with mutex held. Publish an error once, then interrupt the other
    // network thread if it is blocked in a read or write.
    void async_fail(const char* message) {
        if(!worker_failed){fail(message);worker_failed=true;}
        shutdown_sockets();changed.notify_all();
    }
    void stop_worker() {
        if(worker.joinable() || receiver.joinable()) {
            {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
            changed.notify_all();
            shutdown_sockets();
            if(worker.joinable())worker.join();
            if(receiver.joinable())receiver.join();
        }
    }
    ~Impl() {
        stop_worker();
        for (Socket s:sockets) close_socket(s);
#ifdef _WIN32
        if (initialized) WSACleanup();
#endif
    }
};
PeerLink::PeerLink():p(new Impl) {}
PeerLink::~PeerLink(){p->stop_worker();}
unsigned PeerLink::slot() const { return p->slot; }
unsigned PeerLink::players() const { return p->count; }
const std::string& PeerLink::error() const { return p->async_error.empty()?p->error:p->async_error; }
bool PeerLink::input_delay(unsigned ticks) {
    if(p->count || ticks>12)return p->fail("Input delay must be 0..12 before connecting");
    p->delay=ticks;return true;
}
bool PeerLink::prediction_window(unsigned ticks) {
    if(p->count || !ticks || ticks>30)return p->fail("Prediction window must be 1..30 before connecting");
    p->prediction=ticks;return true;
}
bool PeerLink::local_mesh(bool enabled) {
    if(p->count || !p->sockets.empty())return p->fail("Mesh adapter must be configured before connecting");
    p->mesh=enabled;return true;
}
bool PeerLink::host(const std::string& endpoint, unsigned players, uint64_t compatibility, const Admission* admissions) {
    if (p->failed || p->count) return p->fail("Session already initialized or failed");
    if(p->mesh)return p->fail("A mesh participant must join its local adapter");
    sockaddr_in addr{};
    if (players<2 || players>kMaxPeers || !address(endpoint,addr)) return p->fail("Expected IPv4:port and a supported peer count");
    Socket listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (listener==no_socket) return p->fail("Cannot create listener");
    p->sockets.push_back(listener);
    if (bind(listener,(sockaddr*)&addr,sizeof(addr))!=0 || listen(listener,kMaxPeers)!=0) return p->fail("Cannot bind/listen");
    p->host=true; p->count=players;
    std::vector<Socket> ordered(players-1,no_socket);
    for (unsigned slot=1; slot<players; ++slot) {
        if (!ready(listener,false,std::chrono::steady_clock::now()+std::chrono::seconds(30))) return p->fail("Timed out waiting for peer");
        Socket s=accept(listener,nullptr,nullptr);
        if (s==no_socket) return p->fail("Cannot accept peer");
        p->sockets.push_back(s); tune(s);
        Packet hello{};
        if (!transfer(s,hello,false) || !valid(hello,Hello) || get(hello,16,8)!=compatibility || hello[34]!=p->delay || hello[35]!=p->prediction || hello[36]) return p->fail("Incompatible peer handshake");
        const unsigned requested=hello[32];
        if (admissions) {
            if (requested==0 || requested>=players || ordered[requested-1]!=no_socket ||
                memcmp(hello.data()+40,admissions[requested].data(),16)!=0)
                return p->fail("Room admission rejected");
            ordered[requested-1]=s;
        } else {
            if (requested!=kMaxPeers) return p->fail("Unexpected room admission");
            ordered[slot-1]=s;
        }
    }
    close_socket(listener); p->sockets.erase(p->sockets.begin());
    p->sockets=std::move(ordered);
    for (unsigned i=0; i<p->sockets.size(); ++i) {
        Packet welcome=packet(Welcome); welcome[32]=(uint8_t)(i+1); welcome[33]=(uint8_t)players;welcome[34]=(uint8_t)p->delay;
        welcome[35]=(uint8_t)p->prediction;
        if (!transfer(p->sockets[i],welcome,true)) return p->fail("Peer left during handshake");
    }
    return true;
}
bool PeerLink::join(const std::string& endpoint, uint64_t compatibility, unsigned requested_slot, const Admission* admission) {
    if (p->failed || p->count) return p->fail("Session already initialized or failed");
    sockaddr_in addr{};
    if (!address(endpoint,addr)) return p->fail("Expected IPv4:port");
    if(p->mesh && (!admission || (ntohl(addr.sin_addr.s_addr)>>24)!=127))return p->fail("Mesh adapters require loopback and a reserved admission");
    if ((admission && ((!p->mesh && requested_slot==0) || requested_slot>=kMaxPeers)) ||
        (!admission && requested_slot!=kMaxPeers)) return p->fail("Invalid room admission configuration");
    Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s==no_socket) return p->fail("Cannot create peer socket");
    p->sockets.push_back(s); tune(s);
    // Development connections use explicit addresses; invitation/relay
    // discovery is a separate service layer.
    if (!connect_bounded(s,addr)) return p->fail("Cannot connect to host");
    Packet hello=packet(Hello,0,compatibility), welcome{};
    hello[32]=(uint8_t)requested_slot;
    hello[34]=(uint8_t)p->delay;
    hello[35]=(uint8_t)p->prediction;
    hello[36]=p->mesh?1:0;
    if (admission) memcpy(hello.data()+40,admission->data(),16);
    if (!transfer(s,hello,true) || !transfer(s,welcome,false) || !valid(welcome,Welcome)) return p->fail("Handshake failed");
    p->slot=welcome[32]; p->count=welcome[33];
    if (welcome[34]!=p->delay)return p->fail("Host changed the negotiated input delay");
    if (welcome[35]!=p->prediction)return p->fail("Host changed the negotiated prediction window");
    if (welcome[36]!=(p->mesh?1:0))return p->fail("Host changed the negotiated mesh adapter");
    if (admission && p->slot!=requested_slot) return p->fail("Host changed the reserved room slot");
    if (p->count<2 || p->count>kMaxPeers || (!p->mesh && p->slot==0) || p->slot>=p->count) return p->fail("Invalid slot assignment");
    return true;
}
void PeerLink::start_worker() {
    if(!p->worker.joinable()) {
        if(!p->predicting)p->next=1+p->delay;
        if(p->host)p->worker=std::thread([this] {
            for(;;) {
                Impl::Job job;
                {std::unique_lock<std::mutex> lock(p->mutex);
                 p->changed.wait(lock,[&]{return p->stopping || p->worker_failed || !p->jobs.empty();});
                 if(p->stopping || p->worker_failed)return;job=p->jobs.front();p->jobs.pop_front();}
                Inputs bundle;const bool ok=exchange_sync(job.tick,job.hash,job.local.data(),bundle);
                {std::lock_guard<std::mutex> lock(p->mutex);
                 if(!ok)p->worker_failed=true;else p->bundles.emplace(job.tick,Impl::Received{bundle,{}});}
                p->changed.notify_all();if(!ok)return;
            }
        });
        else {
            // Sending future contributions must not wait for the reply to the
            // previous contribution. A single send/receive worker otherwise
            // caps throughput at one simulation tick per network round trip.
            p->worker=std::thread([this] {
                for(;;) {
                    Impl::Job job;
                    {std::unique_lock<std::mutex> lock(p->mutex);
                     p->changed.wait(lock,[&]{return p->stopping || p->worker_failed || !p->jobs.empty();});
                     if(p->stopping || p->worker_failed)return;
                     job=p->jobs.front();p->jobs.pop_front();p->inflight.push_back(job);}
                    p->changed.notify_all();
                    Packet input=packet(Input,job.tick,job.hash);
                    input[32]=job.local[0];input[33]=job.local[1];
                    if(p->mesh){input[24]=uint8_t(job.delay);input[25]=uint8_t(job.lead);}
                    if(!transfer(p->sockets[0],input,true)) {
                        std::lock_guard<std::mutex> lock(p->mutex);
                        p->async_fail("Cannot send peer input");return;
                    }
                }
            });
            p->receiver=std::thread([this] {
                for(;;) {
                    Impl::Job job;
                    {std::unique_lock<std::mutex> lock(p->mutex);
                     p->changed.wait(lock,[&]{return p->stopping || p->worker_failed || !p->inflight.empty();});
                     if(p->stopping || p->worker_failed)return;
                     job=p->inflight.front();p->inflight.pop_front();}
                    Packet bundle{};Inputs inputs{};Pacing pacing{};const char* error=nullptr;
                    if(!transfer(p->sockets[0],bundle,false))error="Host disconnected or input timed out";
                    else if(!valid(bundle,Bundle) || get(bundle,8,8)!=job.tick || get(bundle,16,8)!=job.hash)
                        error="Host tick or state checksum mismatch";
                    else {
                        memcpy(inputs.data(),bundle.data()+32,inputs.size());
                        if(p->mesh) {
                            if(bundle[24]>16 || bundle[25]>16)error="Invalid mesh pacing metadata";
                            else pacing={true,bundle[24],bundle[25]};
                        }
                        if(inputs[p->slot*2]!=job.local[0] || inputs[p->slot*2+1]!=job.local[1])
                            error="Host changed the local input contribution";
                    }
                    {std::lock_guard<std::mutex> lock(p->mutex);
                     if(p->stopping || p->worker_failed)return;
                     if(error){p->async_fail(error);return;}
                     p->bundles.emplace(job.tick,Impl::Received{inputs,pacing});}
                    p->changed.notify_all();
                }
            });
        }
    }
}
bool PeerLink::exchange(uint64_t tick, uint64_t hash, const uint8_t local[2], Inputs& out) {
    if(p->predicting || p->draining)return false; // never race the asynchronous worker
    if(!p->delay)return exchange_sync(tick,hash,local,out);
    std::unique_lock<std::mutex> lock(p->mutex);
    if(!p->count || tick!=p->main_next || p->stopping) {
        p->async_error="Non-sequential buffered input tick";return false;
    }
    if(p->failed)return false;
    start_worker();
    if(!p->worker_failed) {
        if(p->jobs.size()>p->delay+1){p->async_error="Buffered input queue overflow";return false;}
        p->jobs.push_back({tick+p->delay,hash,{local[0],local[1]}});p->changed.notify_all();
    }
    ++p->main_next;
    if(tick<=p->delay) {
        if(p->worker_failed)return false;
        out={};return true;
    }
    if(!p->changed.wait_for(lock,std::chrono::seconds(30),[&]{return p->stopping || p->worker_failed || p->bundles.count(tick);})) {
        p->async_error="Buffered peer input timed out";return false;
    }
    auto ready=p->bundles.find(tick);
    if(ready==p->bundles.end())return false;
    out=ready->second.inputs;p->bundles.erase(ready);return true;
}
bool PeerLink::submit_prediction(uint64_t tick,uint64_t hash,const uint8_t local[2],unsigned delay,unsigned lead) {
    std::lock_guard<std::mutex> lock(p->mutex);
    if(!p->prediction || !p->count || p->stopping || p->worker_failed || p->failed || delay>16 || lead>16)return false;
    if(!p->predicting) {
        if(p->delay && (!p->draining || p->drain_remaining)) {
            p->async_fail("Buffered startup inputs must be drained before prediction");return false;
        }
        if(!p->worker.joinable())p->main_next=p->next;
        p->receive_next=p->main_next;p->predicting=true;
    }
    if(tick!=p->main_next || p->main_next-p->receive_next>=p->prediction) {
        p->async_fail("Non-sequential or over-window predicted input");return false;
    }
    start_worker();
    p->jobs.push_back({tick,hash,{local[0],local[1]},delay,lead});++p->main_next;
    p->changed.notify_all();return true;
}
bool PeerLink::drain_startup(uint64_t tick,Inputs& out) {
    std::unique_lock<std::mutex> lock(p->mutex);
    if(!p->prediction || !p->delay || !p->worker.joinable() || p->predicting || p->stopping || p->failed)return false;
    if(!p->draining){p->draining=true;p->drain_remaining=p->delay;}
    if(!p->drain_remaining || tick!=p->main_next) {
        p->async_fail("Non-sequential startup drain");return false;
    }
    if(!p->changed.wait_for(lock,std::chrono::seconds(30),[&]{return p->stopping || p->worker_failed || p->bundles.count(tick);})) {
        p->async_fail("Startup drain timed out");return false;
    }
    auto found=p->bundles.find(tick);
    if(p->stopping || p->worker_failed || found==p->bundles.end())return false;
    out=found->second.inputs;p->bundles.erase(found);++p->main_next;--p->drain_remaining;return true;
}
PeerLink::Poll PeerLink::poll_prediction(uint64_t& tick,Inputs& out,Pacing* pacing) {
    std::lock_guard<std::mutex> lock(p->mutex);
    if(!p->prediction || p->stopping || p->worker_failed || p->failed)return Poll::Failed;
    auto found=p->bundles.find(p->receive_next);
    if(found==p->bundles.end())return Poll::Pending;
    tick=found->first;out=found->second.inputs;if(pacing)*pacing=found->second.pacing;p->bundles.erase(found);++p->receive_next;
    return Poll::Ready;
}
bool PeerLink::exchange_sync(uint64_t tick, uint64_t hash, const uint8_t local[2], Inputs& out) {
    if (p->failed) return false;
    if (!p->count || tick!=p->next) return p->fail("Non-sequential input tick");
    if (p->host) {
        out={}; out[0]=local[0]; out[1]=local[1];
        for (unsigned i=0; i<p->sockets.size(); ++i) {
            Packet input{};
            if (!transfer(p->sockets[i],input,false)) return p->fail("Peer disconnected or input timed out");
            if (!valid(input,Input) || get(input,8,8)!=tick || get(input,16,8)!=hash) return p->fail("Peer tick or state checksum mismatch");
            out[(i+1)*2]=input[32]; out[(i+1)*2+1]=input[33];
        }
        Packet bundle=packet(Bundle,tick,hash); memcpy(bundle.data()+32,out.data(),out.size());
        for (Socket s:p->sockets) if (!transfer(s,bundle,true)) return p->fail("Cannot deliver input bundle");
    } else {
        Packet input=packet(Input,tick,hash), bundle{};
        input[32]=local[0]; input[33]=local[1];
        if (!transfer(p->sockets[0],input,true) || !transfer(p->sockets[0],bundle,false)) return p->fail("Host disconnected or input timed out");
        if (!valid(bundle,Bundle) || get(bundle,8,8)!=tick || get(bundle,16,8)!=hash) return p->fail("Host tick or state checksum mismatch");
        memcpy(out.data(),bundle.data()+32,out.size());
        if (out[p->slot*2]!=local[0] || out[p->slot*2+1]!=local[1]) return p->fail("Host changed the local input contribution");
    }
    ++p->next;
    return true;
}
}
