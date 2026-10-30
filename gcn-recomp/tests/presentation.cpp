#define SDL_MAIN_HANDLED
#include "desktop_presentation.h"
#include "refresh_clock.h"
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
static int interval=0,calls=0;
static bool fail=false;
extern "C" int SDL_GL_SetSwapInterval(int value) {++calls;if(fail)return -1;interval=value;return 0;}
extern "C" int SDL_GL_GetSwapInterval() {return interval;}
extern "C" const char* SDL_GetError() {return "test driver rejected swap interval";}
static void require(bool ok,const char* why) {if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(int argc,char** argv) {
    require(argc==2,"fresh output directory required");
    const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"fresh output");std::filesystem::create_directories(root);
    const auto config=(root/"presentation.cfg").string();std::string error;
    using namespace desktop_presentation;
    require(load(config,error) && current()==Settings{} && interval==0,"fresh install defaults off");
    require(!std::filesystem::exists(config),"loading defaults does not overwrite user files");
    require(preview({true,144},error) && interval==1 && save(error),"enable VSync and persist 144 Hz");
    require(preview({},error) && load(config,error) && current()==Settings{true,144} && interval==1,"load saved settings");
    fail=true;require(!preview({},error) && !error.empty() && current()==Settings{true,144},"driver failure keeps active settings");fail=false;
    require(!preview({false,61},error) && current()==Settings{true,144},"invalid rate cannot replace settings");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 1\n1 144 trailing\n";}
    require(!load(config,error) && current()==Settings{} && interval==0,"malformed config falls back off");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 999\n1 144\n";}
    require(!load(config,error) && current()==Settings{},"unsupported version keeps safe defaults");
    for(const int hz:{120,144,165,240}) {
        RefreshClock clock;const uint64_t start=123456789;
        require(clock.due(start,hz),"first refresh immediate");
        for(int slot=1;slot<=hz*60;++slot) {
            const uint64_t expected=start+(uint64_t(slot)*1000000000ULL+hz-1)/hz;
            require(clock.deadline()==expected,"absolute schedule has no accumulated rounding drift");
            require(!clock.due(expected-1,hz) && clock.due(expected,hz),"one output per deadline");
        }
        const auto late=clock.deadline()+100000000;
        require(clock.due(late,hz) && clock.deadline()>late && !clock.due(late,hz),"stall skips missed slots without catch-up bursts");
        require(clock.due(late,120+hz),"rate change reanchors");
        require(clock.due(late,0),"native mode disables refresh schedule");
    }
    printf("PASS: presentation defaults, persistence, driver errors and 60-second fractional refresh schedules (%d driver calls)\n",calls);
}
