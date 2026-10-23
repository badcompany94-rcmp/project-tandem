#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "input_mesh.h"
#include "redundant_input.h"
#include <functional>
#include <memory>
#include <string>

namespace net::mesh {
// Single-threaded loopback boundary shared by the EOS adapter and native tests.
// Caller owns Winsock initialization and authenticated peer transport.
class LoopbackMesh {
public:
    using Send=std::function<void(unsigned,const std::vector<uint8_t>&)>;
    LoopbackMesh(unsigned count,unsigned slot,Admission admission,Send send,bool redundant=false):mesh_(count,slot,admission),send_(std::move(send)),count_(count),slot_(slot),use_redundancy_(redundant) {
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener_==INVALID_SOCKET)throw std::runtime_error("Mesh loopback socket failed");
        BOOL exclusive=TRUE;setsockopt(listener_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener_,(sockaddr*)&address,sizeof(address)) || listen(listener_,1)) {
            closesocket(listener_);listener_=INVALID_SOCKET;throw std::runtime_error("Mesh loopback listener failed");
        }
        int size=sizeof(address);getsockname(listener_,(sockaddr*)&address,&size);port_=ntohs(address.sin_port);
        try{nonblocking(listener_);}catch(...){closesocket(listener_);listener_=INVALID_SOCKET;throw;}
    }
    ~LoopbackMesh(){if(native_!=INVALID_SOCKET)closesocket(native_);if(listener_!=INVALID_SOCKET)closesocket(listener_);}
    LoopbackMesh(const LoopbackMesh&)=delete;
    LoopbackMesh& operator=(const LoopbackMesh&)=delete;
    unsigned port()const{return port_;}
    bool closed()const{return closed_;}
    void disconnect(){
        closed_=true;
        if(native_!=INVALID_SOCKET){closesocket(native_);native_=INVALID_SOCKET;}
        if(listener_!=INVALID_SOCKET){closesocket(listener_);listener_=INVALID_SOCKET;}
        incoming_.clear();pending_.clear();
    }
    void peer(unsigned sender,const std::vector<uint8_t>& packet){if(!closed_){
        if(!packet.empty() && packet[0]==14) {
            if(!redundant_)throw std::runtime_error("Unnegotiated redundant input");
            redundant_->peer(sender,packet);
        }else mesh_.peer(sender,packet);
        collect();
    }}
    void pump(uint64_t now) {
        if(closed_)return;
        if(!started_){started_=true;progress_=now;}
        if(native_==INVALID_SOCKET) {
            native_=accept(listener_,nullptr,nullptr);
            if(native_!=INVALID_SOCKET) {
                nonblocking(native_);int one=1;setsockopt(native_,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof(one));progress_=now;
                closesocket(listener_);listener_=INVALID_SOCKET;
            }else if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("Mesh loopback accept failed");
        }
        if(native_!=INVALID_SOCKET) {
            char data[4096];const int received=recv(native_,data,sizeof(data),0);
            if(received>0) {
                incoming_.append(data,received);
                while(incoming_.size()>=64) {
                    Packet packet;std::copy_n((const uint8_t*)incoming_.data(),64,packet.begin());incoming_.erase(0,64);
                    mesh_.native(packet);
                    if(use_redundancy_) {
                        if(valid(packet,1))redundant_=std::make_unique<RedundantInput>(count_,slot_,uint64_t(packet[34])+1,send_,[this](unsigned from,const auto& bytes){mesh_.peer(from,bytes);});
                        else if(valid(packet,3))redundant_->native(packet);
                    }
                    collect();progress_=now;
                }
            }else if(!received || WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAECONNABORTED){close_native();return;}
            else if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("Mesh native input disconnected");
        }
        if(now>=retry_at_){mesh_.retry();collect();retry_at_=now+500;}
        // Acknowledgment-only datagrams must not race a remote native startup.
        // The reliable handshake proves every peer has initialized its decoder.
        if(redundant_ && mesh_.welcomed())redundant_->pump(now);
        if(native_!=INVALID_SOCKET && !pending_.empty()) {
            const int sent=send(native_,pending_.data(),int(pending_.size()),0);
            if(sent>0){pending_.erase(0,sent);progress_=now;}
            else if(!sent || WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAECONNABORTED){close_native();return;}
            else if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("Mesh native output disconnected");
        }
        if(now-progress_>30000)throw std::runtime_error("Mesh native handshake or input timed out");
    }
private:
    InputMesh mesh_;Send send_;
    unsigned count_,slot_;bool use_redundancy_;
    std::unique_ptr<RedundantInput> redundant_;
    SOCKET listener_=INVALID_SOCKET,native_=INVALID_SOCKET;
    unsigned port_=0;
    bool closed_=false,started_=false;
    uint64_t progress_=0,retry_at_=0;
    std::string incoming_,pending_;
    void close_native(){disconnect();}
    static void nonblocking(SOCKET socket) {u_long one=1;if(ioctlsocket(socket,FIONBIO,&one))throw std::runtime_error("Mesh loopback nonblocking failed");}
    void collect() {
        for(auto& message:mesh_.outgoing())if(!redundant_ || message.bytes[0]!=12)send_(message.peer,message.bytes);
        for(const auto& packet:mesh_.output()) {
            if(pending_.size()+packet.size()>65536)throw std::runtime_error("Mesh loopback output queue full");
            pending_.append((const char*)packet.data(),packet.size());
        }
    }
};
}
#endif
