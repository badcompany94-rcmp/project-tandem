// SDL's dummy video driver exercises mode transitions without changing a monitor.
#define SDL_MAIN_HANDLED
#include "desktop_display.h"
#include "input_defaults.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <SDL.h>
using namespace desktop_display;
static int checks;
static void require(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL: %s: %s\n",why,SDL_GetError());exit(1);}}
int main(int argc,char** argv) {
    require(argc==2,"fresh test directory required");std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root),"test output must be fresh");std::filesystem::create_directories(root);
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);require(SDL_Init(SDL_INIT_VIDEO)==0,"initialize dummy video");
    require(std::string(SDL_GetCurrentVideoDriver())=="dummy","no physical monitor is affected");
    auto* window=SDL_CreateWindow("Display test",0,0,960,540,SDL_WINDOW_RESIZABLE);require(window,"create dummy window");
    const auto config=(root/"display.cfg").string();std::string error;
    require(init(window,config,error),"initialize display settings");require(current().mode==Mode::Windowed,"detect initial windowed mode");
    auto next=current();next.width=1280;next.height=720;require(preview(next,100,error),"preview resolution");
    int w,h;SDL_GetWindowSize(window,&w,&h);require(w==1280 && h==720,"SDL window adopts requested resolution");
    require(pending() && seconds_left(100)==15,"confirmation countdown starts");require(!std::filesystem::exists(config),"unconfirmed settings are not saved");
    require(!tick(15099,error) && tick(15100,error),"unconfirmed settings revert on deadline");SDL_GetWindowSize(window,&w,&h);require(w==960 && h==540 && !pending(),"previous resolution restored");
    require(preview(next,20000,error) && keep(error),"confirm windowed resolution");require(std::filesystem::exists(config),"confirmed settings saved");
    require(init(window,config,error) && current()==next,"saved settings reload");
    next.mode=Mode::Borderless;require(preview(next,40000,error),"borderless preview");require((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP,"SDL desktop fullscreen flag is distinct");
    require(revert(error) && current().mode==Mode::Windowed,"explicit revert returns to windowed");
    const auto available=resolutions(Mode::Fullscreen,0);require(!available.empty(),"enumerate supported fullscreen resolutions");
    next.mode=Mode::Fullscreen;next.width=available[0].width;next.height=available[0].height;
    require(preview(next,50000,error),"exclusive fullscreen preview");
    require((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN) && (SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)!=SDL_WINDOW_FULLSCREEN_DESKTOP,"SDL exclusive fullscreen flag is distinct");
    require(keep(error) && init(window,config,error) && current()==next,"fullscreen choice persists");
    auto invalid=next;invalid.monitor=999;require(!preview(invalid,60000,error) && !error.empty() && current()==next && !pending(),"invalid monitor cannot replace working settings");
    invalid=next;invalid.width=321;invalid.height=241;require(!preview(invalid,60000,error) && current()==next,"unsupported exclusive resolution rejected");
    next.mode=Mode::Windowed;next.width=1280;next.height=720;require(preview(next,70000,error) && keep(error),"return from fullscreen to windowed");
    {std::ofstream corrupt(config,std::ios::trunc);corrupt<<"GCN_DISPLAY 999\n";}
    require(!init(window,config,error) && !error.empty() && current().mode==Mode::Windowed,"malformed settings keep usable launch state");
    input_set_default("desktop_resolution_aspect_num",16);input_set_default("desktop_resolution_aspect_den",9);
    const auto presets=resolutions(Mode::Windowed,0);require(presets.size()>=6,"common widescreen presets are available");
    for(const auto& r:presets)require(r.width*9==r.height*16,"resolution filter only offers the requested aspect ratio");
    shutdown();SDL_DestroyWindow(window);SDL_Quit();printf("PASS: %d display mode, resolution, rollback and persistence checks\n",checks);return 0;
}
