#define SDL_MAIN_HANDLED
#include "desktop_presentation.h"
#include "desktop_graphics.h"
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
    require(effective_scale(4,2)==4 && effective_scale(2,4)==4,"default preserves launch/menu supersampling");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 1\n1 144\n";}
    require(load(config,error) && current()==Settings{true,144,0},"legacy settings retain VSync/interpolation and default scaling");
    require(preview({false,0,1},error) && effective_scale(4,6)==1 && save(error),"explicit 1x overrides launch and menu supersampling");
    require(load(config,error) && current()==Settings{false,0,1},"new internal scale persists through reload");
    require(!preview({false,0,5},error) && current().internal_scale==1,"invalid scale cannot replace active settings");
    for(int scale=0;scale<=4;++scale)require(preview({false,0,scale},error) && save(error) && load(config,error) && current().internal_scale==scale,"all scale options persist");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 2\n1 144 99\n";}
    require(!load(config,error) && current()==Settings{},"out-of-range saved scale uses safe defaults");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 2\n1 144\n";}
    require(!load(config,error) && current()==Settings{},"truncated new settings cannot change rendering");
    require(preview({true,144},error) && interval==1 && save(error),"enable VSync and persist 144 Hz");
    require(preview({},error) && load(config,error) && current()==Settings{true,144} && interval==1,"load saved settings");
    fail=true;require(!preview({},error) && !error.empty() && current()==Settings{true,144},"driver failure keeps active settings");fail=false;
    require(!preview({false,61},error) && current()==Settings{true,144},"invalid rate cannot replace settings");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 1\n1 144 trailing\n";}
    require(!load(config,error) && current()==Settings{} && interval==0,"malformed config falls back off");
    {std::ofstream out(config);out<<"GCN_PRESENTATION 999\n1 144\n";}
    require(!load(config,error) && current()==Settings{},"unsupported version keeps safe defaults");
    desktop_graphics::initialize("Vendor\nLine","llvmpipe (LLVM)","4.5 test driver","4.50","Windows","2.32");
    desktop_graphics::update({1280,720,2560,1440,1,0,0});
    const auto report=desktop_graphics::report();
    require(desktop_graphics::software_renderer() && report.find("Active internal scale: 1x")!=std::string::npos && report.find("Drawable: 2560x1440")!=std::string::npos,"diagnostics distinguish drawable size and active scale and detect known software rendering");
    require(report.find("Vendor\nLine")==std::string::npos,"driver text cannot insert extra diagnostic fields");
    desktop_graphics::initialize("AMD","AMD Radeon","test driver","3.30","Windows","2.32");
    require(!desktop_graphics::software_renderer(),"hardware renderer is not mislabeled as known software");
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
    // Regression: idle refresh polls used to consume slots without showing
    // anything, dropping native-only menus/effects to irregular sub-60 output.
    for(const unsigned hz:{120u,144u,165u,240u}) {
        RefreshClock clock;unsigned shown=0;
        for(uint64_t frame=0;frame<600;++frame) {
            const uint64_t now=1000000000ULL+frame*16683333ULL;
            require(!clock.extra_frame(now-1000000,hz,false),"fallback idle cannot consume an output slot");
            shown+=clock.native_frame(now,hz,false);
            require(clock.deadline()==0,"native fallback clears interpolation schedule");
        }
        require(shown==600,"every native fallback frame is displayed at every target rate");
        require(clock.native_frame(12000000000ULL,hz,true),"matching geometry resumes immediately");
        require(!clock.extra_frame(12000000000ULL,hz,true),"no duplicate output at the same deadline");
        require(clock.extra_frame(clock.deadline(),hz,true),"interpolation resumes on its next deadline");
    }
    printf("PASS: presentation defaults, persistence, driver errors and 60-second fractional refresh schedules (%d driver calls)\n",calls);
}
