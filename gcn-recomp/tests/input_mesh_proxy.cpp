// Loopback-only transport fixture. The Python router supplies authenticated
// participant slots and ordered delivery, replacing the production P2P service.
#include "net/input_mesh_socket.h"
#include <mmsystem.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    try {
        const unsigned slot=unsigned(atoi(argv[1])),count=unsigned(atoi(argv[2])),router_port=unsigned(atoi(argv[3]));
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 3;
        timeBeginPeriod(1);
        SOCKET udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(udp==INVALID_SOCKET)return 4;
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(udp,(sockaddr*)&local,sizeof(local)))return 5;
        int length=sizeof(local);getsockname(udp,(sockaddr*)&local,&length);
        sockaddr_in router=local;router.sin_port=htons(u_short(router_port));
        u_long nonblocking=1;ioctlsocket(udp,FIONBIO,&nonblocking);
        const char* text=getenv("GCN_TEST_ADMISSION");if(!text || strlen(text)!=32)throw std::runtime_error("Missing local fixture admission");
        net::Admission admission{};
        for(unsigned i=0;i<16;++i)admission[i]=uint8_t(std::stoul(std::string(text+2*i,2),nullptr,16));
        net::mesh::LoopbackMesh mesh(count,slot,admission,[&](unsigned peer,const auto& payload) {
            std::vector<uint8_t> bytes{uint8_t(peer)};bytes.insert(bytes.end(),payload.begin(),payload.end());
            if(sendto(udp,(const char*)bytes.data(),int(bytes.size()),0,(sockaddr*)&router,sizeof(router))!=int(bytes.size()))throw std::runtime_error("Fixture UDP send failed");
        },getenv("GCN_TEST_REDUNDANCY")!=nullptr);
        {std::ofstream file(argv[4]);file<<"{\"tcp\":"<<mesh.port()<<",\"udp\":"<<ntohs(local.sin_port)<<"}\n";}
        const auto started=std::chrono::steady_clock::now();
        while(!mesh.closed()) {
            auto now=std::chrono::steady_clock::now();
            auto ms=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count());
            if(ms>240000)throw std::runtime_error("Fixture lifetime exceeded");
            mesh.pump(ms);
            for(unsigned i=0;i<128;++i) {
                char bytes[1200];sockaddr_in source{};int size=sizeof(source);
                const int n=recvfrom(udp,bytes,sizeof(bytes),0,(sockaddr*)&source,&size);
                if(n<0){if(WSAGetLastError()==WSAEWOULDBLOCK || WSAGetLastError()==WSAECONNRESET)break;throw std::runtime_error("Fixture UDP receive failed");}
                if(source.sin_addr.s_addr!=router.sin_addr.s_addr || source.sin_port!=router.sin_port || n<2)throw std::runtime_error("Unauthenticated fixture datagram");
                mesh.peer(uint8_t(bytes[0]),std::vector<uint8_t>((uint8_t*)bytes+1,(uint8_t*)bytes+n));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        closesocket(udp);timeEndPeriod(1);WSACleanup();puts("PASS: native mesh socket closed");
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
