#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "lan_runtime.h"
#include "eos_bridge.h"
#include "eos_local_http.h"
#include "eos_poll_wait.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>

int main(int argc,char** argv){
    using namespace mkdd::eos;
    setvbuf(stdout,nullptr,_IONBF,0);
    try{
        if(argc<4 || argc>6 || std::string(argv[1])!="--serve"){
            fputs("Usage: mkdd_lan --serve <upstream-port> <ready-file> [host-ip|-] [lan-port]\n",stderr);return 2;
        }
        const unsigned upstream=unsigned(std::stoul(argv[2]));if(!upstream || upstream>65535)throw std::runtime_error("invalid upstream port");
        const std::string ready=argv[3],host=argc>=5 && std::string(argv[4])!="-"?argv[4]:"";
        const unsigned port=argc==6?unsigned(std::stoul(argv[5])):28122;
        LanRuntime runtime(host,port);Bridge bridge(runtime,upstream);LocalHttp http(0,false,"lan-private-test");
        auto path=std::filesystem::u8path(ready),temporary=std::filesystem::u8path(ready+".tmp");
        auto data=Json::dict({{"process_id",(int64_t)GetCurrentProcessId()},{"port",(int)http.port()},{"protocol",1},{"scope","lan-private-test"}}).dump();
        {std::ofstream file(temporary,std::ios::binary);file<<data;if(!file)throw std::runtime_error("Cannot write LAN readiness record");}
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish LAN readiness record");
        puts("Project Tandem LAN helper ready (private experimental build)");
        PollWait wait;auto next=std::chrono::steady_clock::now();uint64_t sequence=0;
        while(!std::filesystem::exists(std::filesystem::u8path(ready+".stop"))){
            runtime.tick();http.poll([&](const Json& request){return bridge.request(request);});
            if(std::chrono::steady_clock::now()>=next){
                next=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
                auto stats=std::filesystem::u8path(ready+".stats.json"),scratch=std::filesystem::u8path(ready+".stats.tmp");
                {std::ofstream file(scratch,std::ios::binary);file<<bridge.statistics(++sequence).dump();}
                MoveFileExW(scratch.c_str(),stats.c_str(),MOVEFILE_REPLACE_EXISTING);
            }
            wait.wait();
        }
        runtime.leave();return 0;
    }catch(const std::exception& e){fprintf(stderr,"LAN ERROR: %s\n",e.what());return 1;}
}
