#include "desktop_display.h"
#include "input_defaults.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace desktop_display {
namespace {
SDL_Window* window;
Settings active,previous;
std::string path;
uint64_t deadline=0;
int previous_x=0,previous_y=0;
constexpr uint64_t confirmation_ms=15000;
bool valid(const Settings& s){return (int)s.mode>=0 && (int)s.mode<=2 && s.monitor>=0 && s.monitor<SDL_GetNumVideoDisplays() && s.width>=320 && s.height>=240 && s.width<=7680 && s.height<=4320;}
bool apply_window(const Settings& s,std::string& error) {
    if(!window || !valid(s)){error="The selected monitor or resolution is unavailable.";return false;}
    SDL_DisplayMode chosen{},desktop{};
    if(SDL_GetDesktopDisplayMode(s.monitor,&desktop)<0){error=SDL_GetError();return false;}
    if(s.mode==Mode::Fullscreen) {
        SDL_DisplayMode wanted=desktop;wanted.w=s.width;wanted.h=s.height;
        if(!SDL_GetClosestDisplayMode(s.monitor,&wanted,&chosen) || chosen.w!=s.width || chosen.h!=s.height) {
            error="This monitor does not support that fullscreen resolution.";return false;
        }
    }
    if(SDL_SetWindowFullscreen(window,0)<0){error=SDL_GetError();return false;}
    if(SDL_SetWindowDisplayMode(window,s.mode==Mode::Fullscreen?&chosen:nullptr)<0){error=SDL_GetError();return false;}
    SDL_RestoreWindow(window);
    SDL_SetWindowSize(window,s.width,s.height);
    SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED_DISPLAY(s.monitor),SDL_WINDOWPOS_CENTERED_DISPLAY(s.monitor));
    const Uint32 flags=s.mode==Mode::Fullscreen?SDL_WINDOW_FULLSCREEN:s.mode==Mode::Borderless?SDL_WINDOW_FULLSCREEN_DESKTOP:0;
    if(SDL_SetWindowFullscreen(window,flags)<0){error=SDL_GetError();return false;}
    active=s;return true;
}
bool write(std::string& error) {
    const std::filesystem::path target(path),temporary=path+".tmp";
    std::error_code ec;if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);
    if(ec){error="Cannot create the display settings folder.";return false;}
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    const char* name=SDL_GetDisplayName(active.monitor);
    out<<"GCN_DISPLAY 1\n"<<(int)active.mode<<' '<<active.monitor<<' '<<active.width<<' '<<active.height<<' '<<std::quoted(name?name:"")<<'\n';
    out.flush();bool ok=(bool)out;out.close();ok=ok && !out.fail();
    if(!ok){error="Cannot save display settings. Check the folder is writable.";return false;}
#ifdef _WIN32
    ok=MoveFileExW(temporary.wstring().c_str(),target.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,target,ec);ok=!ec;
#endif
    if(!ok){error="Cannot replace display settings. Previous settings were preserved.";return false;}
    fprintf(stderr,"[display-settings] saved mode=%d monitor=%d %dx%d\n",(int)active.mode,active.monitor,active.width,active.height);return true;
}
}
Settings current(){return active;}
std::vector<Monitor> monitors() {
    std::vector<Monitor> out;
    for(int i=0;i<SDL_GetNumVideoDisplays();++i) {
        SDL_DisplayMode mode{};if(SDL_GetDesktopDisplayMode(i,&mode)<0)continue;
        const char* name=SDL_GetDisplayName(i);out.push_back({i,mode.w,mode.h,name?name:"Monitor"});
    }return out;
}
std::vector<Resolution> resolutions(Mode mode,int monitor) {
    std::vector<Resolution> out;
    const int aspect_num=(int)input_default("desktop_resolution_aspect_num",0),aspect_den=(int)input_default("desktop_resolution_aspect_den",0);
    auto add=[&](int w,int h){if(aspect_num>0 && aspect_den>0 && w*aspect_den!=h*aspect_num)return;Resolution r{w,h};if(w>=320 && h>=240 && w<=7680 && h<=4320 && std::find(out.begin(),out.end(),r)==out.end())out.push_back(r);};
    if(mode==Mode::Windowed) {
        for(const auto r:{Resolution{640,480},{960,540},{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}})add(r.width,r.height);
        add(active.width,active.height);
    }
    for(int i=0;i<SDL_GetNumDisplayModes(monitor);++i){SDL_DisplayMode m{};if(SDL_GetDisplayMode(monitor,i,&m)==0)add(m.w,m.h);}
    SDL_DisplayMode desktop{};if(SDL_GetDesktopDisplayMode(monitor,&desktop)==0)add(desktop.w,desktop.h);
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.width!=b.width?a.width<b.width:a.height<b.height;});return out;
}
bool init(SDL_Window* win,const std::string& file,std::string& error) {
    window=win;path=file;deadline=0;error.clear();
    active={};active.monitor=std::max(0,SDL_GetWindowDisplayIndex(win));
    SDL_GetWindowSize(win,&active.width,&active.height);
    const auto flags=SDL_GetWindowFlags(win);
    if((flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP){active.mode=Mode::Borderless;active.width=1280;active.height=720;}
    else if(flags&SDL_WINDOW_FULLSCREEN)active.mode=Mode::Fullscreen;
    previous=active;std::error_code ec;
    if(!std::filesystem::exists(path,ec))return true;
    if(std::filesystem::file_size(path,ec)>4096 || ec){error="Display settings are unreadable.";return false;}
    std::ifstream in(path);std::string tag,name;int version,mode;Settings saved;
    if(!(in>>tag>>version>>mode>>saved.monitor>>saved.width>>saved.height>>std::quoted(name)) || tag!="GCN_DISPLAY" || version!=1 || mode<0 || mode>2) {
        error="Unsupported display settings format.";return false;
    }
    saved.mode=(Mode)mode;
    // Prefer the saved index when identical monitors have the same SDL name.
    if(saved.monitor<0 || saved.monitor>=SDL_GetNumVideoDisplays() || (SDL_GetDisplayName(saved.monitor) && name!=SDL_GetDisplayName(saved.monitor))) {
        saved.monitor=0;for(const auto& m:monitors())if(m.name==name){saved.monitor=m.index;break;}
    }
    if(!valid(saved)){error="Saved display settings are invalid. Using the launch defaults.";return false;}
    // Hidden render/validation windows never change the user's desktop mode.
    if(flags&SDL_WINDOW_HIDDEN)return true;
    const auto fallback=active;
    if(!apply_window(saved,error)) {
        std::string ignored;if(!apply_window(fallback,ignored)) {Settings safe;apply_window(safe,ignored);}
        return false;
    }
    previous=active;return true;
}
bool preview(Settings next,uint64_t now,std::string& error) {
    error.clear();if(deadline){error="Keep or revert the current display change first.";return false;}
    previous=active;SDL_GetWindowPosition(window,&previous_x,&previous_y);
    if(!apply_window(next,error)) {
        std::string ignored;if(!apply_window(previous,ignored)){Settings safe;apply_window(safe,ignored);}
        return false;
    }
    deadline=now+confirmation_ms;return true;
}
bool keep(std::string& error) {
    error.clear();if(!deadline)return true;
    if(!write(error))return false;
    deadline=0;previous=active;return true;
}
bool revert(std::string& error) {
    error.clear();if(!deadline)return true;
    deadline=0;
    Settings restore=previous;if(restore.monitor>=SDL_GetNumVideoDisplays())restore.monitor=0;
    if(!apply_window(restore,error)) {Settings safe;std::string ignored;apply_window(safe,ignored);return false;}
    if(restore.mode==Mode::Windowed) {
        SDL_Rect old{previous_x,previous_y,restore.width,restore.height};bool visible=false;
        for(int i=0;i<SDL_GetNumVideoDisplays();++i){SDL_Rect bounds{},intersection{};
            if(SDL_GetDisplayUsableBounds(i,&bounds)==0 && SDL_IntersectRect(&old,&bounds,&intersection) && intersection.w>=64 && intersection.h>=64)visible=true;}
        if(visible)SDL_SetWindowPosition(window,previous_x,previous_y);
    }
    return true;
}
bool tick(uint64_t now,std::string& error){if(deadline && now>=deadline){revert(error);return true;}return false;}
bool pending(){return deadline!=0;}
unsigned seconds_left(uint64_t now){return deadline>now?(unsigned)((deadline-now+999)/1000):0;}
void shutdown(){if(deadline){std::string ignored;revert(ignored);}window=nullptr;deadline=0;}
}
