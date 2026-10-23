#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "eos_service.h"
#include "eos_runtime.h"
#include "eos_bridge.h"
#include "eos_local_http.h"
#include "eos_poll_wait.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace mkdd::eos {
int serve(const std::string& config,unsigned upstream,const std::string& ready,unsigned seconds,bool fixture) {
    if(!upstream || upstream>65535)throw std::runtime_error("invalid upstream port");
    std::unique_ptr<Runtime> runtime;std::unique_ptr<Bridge> bridge;
    if(!fixture) {
        runtime=std::make_unique<Runtime>(Config::load(config));runtime->login();
        bridge=std::make_unique<Bridge>(*runtime,upstream);
    }
    LocalHttp http(0,!fixture);
    const auto file=std::filesystem::u8path(ready),temporary=std::filesystem::u8path(ready+".tmp");
    const auto state=Json::dict({{"process_id",(int64_t)GetCurrentProcessId()},{"port",(int)http.port()},
                                 {"protocol",1},{"scope",fixture?"ipc-fixture":"eos-internet-playtest"}}).dump();
    {std::ofstream output(temporary,std::ios::binary);output<<state;if(!output)throw std::runtime_error("Cannot write EOS helper readiness record");}
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish EOS helper readiness record");
    puts(fixture?"Loopback protocol fixture ready (no EOS connection)":"Project Tandem online helper ready");fflush(stdout);
    const auto started=std::chrono::steady_clock::now();
    auto next_statistics=started;uint64_t statistics_sequence=0;
    PollWait poll_wait;
    auto timing_started=started,previous_loop=started;
    unsigned timing_loops=0;double longest_loop_ms=0;
    while(!std::filesystem::exists(std::filesystem::u8path(ready+".stop")) &&
          (!seconds || std::chrono::steady_clock::now()-started<std::chrono::seconds(seconds))) {
        const auto loop_now=std::chrono::steady_clock::now();
        const double interval_ms=std::chrono::duration<double,std::milli>(loop_now-previous_loop).count();
        if(interval_ms>longest_loop_ms)longest_loop_ms=interval_ms;
        previous_loop=loop_now;++timing_loops;
        if(loop_now-timing_started>=std::chrono::seconds(2)) {
            fprintf(stderr,"[online-poll] mean=%.2f ms max=%.2f ms\n",
                std::chrono::duration<double,std::milli>(loop_now-timing_started).count()/timing_loops,longest_loop_ms);
            timing_started=loop_now;timing_loops=0;longest_loop_ms=0;
        }
        if(runtime)runtime->tick();
        http.poll([&](const Json& request){return bridge?bridge->request(request):local_request(upstream,request);});
        if(bridge && std::chrono::steady_clock::now()>=next_statistics) {
            next_statistics=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            const auto statistics=std::filesystem::u8path(ready+".stats.json"),scratch=std::filesystem::u8path(ready+".stats.tmp");
            const auto data=bridge->statistics(++statistics_sequence).dump();
            {std::ofstream output(scratch,std::ios::binary);output<<data;}
            MoveFileExW(scratch.c_str(),statistics.c_str(),MOVEFILE_REPLACE_EXISTING);
        }
        poll_wait.wait();
    }
    if(runtime)runtime->leave();return 0;
}
}
