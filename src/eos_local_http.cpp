#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include "eos_local_http.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace mkdd::eos {
namespace {
using Clock=std::chrono::steady_clock;
bool blocked(){return WSAGetLastError()==WSAEWOULDBLOCK;}
void nonblocking(SOCKET s){u_long value=1;if(ioctlsocket(s,FIONBIO,&value))throw std::runtime_error("loopback-socket-failed");}
std::string lower(std::string s){for(char& c:s)if(c>='A' && c<='Z')c+=32;return s;}
}
struct LocalHttp::Impl {
    struct Client {SOCKET socket;std::string input,output;Clock::time_point started=Clock::now();};
    SOCKET listener=INVALID_SOCKET;unsigned port=0;bool eos_ready=false;std::vector<Client> clients;
    ~Impl(){for(auto& c:clients)closesocket(c.socket);if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();}
};
LocalHttp::LocalHttp(unsigned port,bool eos_ready):impl_(std::make_unique<Impl>()) {
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock startup failed");
    auto& p=*impl_;p.eos_ready=eos_ready;p.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(p.listener==INVALID_SOCKET)throw std::runtime_error("loopback-socket-failed");
    BOOL exclusive=TRUE;setsockopt(p.listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)port);
    if(bind(p.listener,(sockaddr*)&address,sizeof(address)) || listen(p.listener,16))throw std::runtime_error("loopback-listen-failed");
    int length=sizeof(address);getsockname(p.listener,(sockaddr*)&address,&length);p.port=ntohs(address.sin_port);nonblocking(p.listener);
}
LocalHttp::~LocalHttp()=default;
unsigned LocalHttp::port()const{return impl_->port;}
void LocalHttp::poll(const std::function<Json(const Json&)>& handler) {
    auto& p=*impl_;
    for(unsigned i=0;i<16;++i) {
        SOCKET s=accept(p.listener,nullptr,nullptr);if(s==INVALID_SOCKET)break;
        if(p.clients.size()>=16){closesocket(s);continue;}
        nonblocking(s);p.clients.push_back({s,{},{}});
    }
    for(auto it=p.clients.begin();it!=p.clients.end();) {
        auto& c=*it;bool close=Clock::now()-c.started>std::chrono::seconds(10);
        if(c.output.empty() && !close) {
            char block[4096];int n=recv(c.socket,block,sizeof(block),0);
            if(n>0)c.input.append(block,n);else if(!n || !blocked())close=true;
            if(c.input.size()>12288)close=true;
            const auto end=c.input.find("\r\n\r\n");
            if(!close && end!=std::string::npos) {
                try {
                    if(end>8192)throw std::runtime_error("invalid-http-header");
                    const std::string headers=lower(c.input.substr(0,end));
                    const auto line=c.input.find("\r\n");const std::string method=c.input.substr(0,line);
                    Json result;unsigned status=200;size_t length=0;
                    const bool health=method=="GET /health HTTP/1.1";
                    if(!health && method!="POST /v1/room HTTP/1.1")throw std::runtime_error("not-found");
                    if(headers.find("\r\ntransfer-encoding:")!=std::string::npos)throw std::runtime_error("invalid-http-header");
                    if(!health) {
                        const std::string key="\r\ncontent-length:";const auto start=headers.find(key);
                        if(start==std::string::npos || headers.find(key,start+1)!=std::string::npos)throw std::runtime_error("invalid-size");
                        const auto finish=headers.find("\r\n",start+2);std::string value=headers.substr(start+key.size(),finish==std::string::npos?std::string::npos:finish-start-key.size());
                        while(!value.empty() && value.front()==' ')value.erase(0,1);
                        if(value.empty() || value.size()>4 || value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid-size");
                        length=std::stoul(value);if(!length || length>4096)throw std::runtime_error("invalid-size");
                    }
                    if(c.input.size()<end+4+length){++it;continue;}
                    if(c.input.size()!=end+4+length)throw std::runtime_error("invalid-http-body");
                    if(health)result=Json::dict({{"service","mkdd-eos-rooms"},{"protocol",1},{"scope",p.eos_ready?"eos-internet-playtest":"ipc-fixture"}});
                    else result=handler(Json::parse(c.input.substr(end+4,length)));
                    if(result.has("error"))status=400;
                    const std::string body=result.dump();
                    c.output="HTTP/1.1 "+std::to_string(status)+(status==200?" OK":" Bad Request")+"\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
                }catch(const std::exception&) {
                    const std::string body="{\"error\":\"invalid-request\"}";
                    c.output="HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
                }
            }
        }
        if(!c.output.empty() && !close) {
            int n=send(c.socket,c.output.data(),(int)c.output.size(),0);
            if(n>0){c.output.erase(0,n);if(c.output.empty())close=true;}else if(!blocked())close=true;
        }
        if(close){closesocket(c.socket);it=p.clients.erase(it);}else ++it;
    }
}
Json local_request(unsigned port,const Json& request) {
    struct Handle {HINTERNET value;~Handle(){if(value)WinHttpCloseHandle(value);}explicit Handle(HINTERNET h):value(h){if(!h)throw std::runtime_error("service-unavailable");}};
    const std::string body=request.dump();if(body.size()>4096 || !port || port>65535)throw std::runtime_error("invalid-room-request");
    Handle session(WinHttpOpen(L"Native EOS room adapter/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session.value,2000,2000,2000,2000);
    Handle connection(WinHttpConnect(session.value,L"127.0.0.1",(INTERNET_PORT)port,0));
    Handle http(WinHttpOpenRequest(connection.value,L"POST",L"/v1/room",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(http.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    if(!WinHttpSendRequest(http.value,L"Content-Type: application/json\r\n",(DWORD)-1L,(void*)body.data(),(DWORD)body.size(),(DWORD)body.size(),0) || !WinHttpReceiveResponse(http.value,nullptr))throw std::runtime_error("service-unavailable");
    std::string reply;
    for(;;) {
        char data[4096];DWORD count=0;
        if(!WinHttpReadData(http.value,data,sizeof(data),&count))throw std::runtime_error("service-unavailable");
        if(!count)break;if(reply.size()+count>65536)throw std::runtime_error("invalid-room-response");reply.append(data,count);
    }
    return Json::parse(reply);
}
}
