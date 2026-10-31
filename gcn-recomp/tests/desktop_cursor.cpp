#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include "desktop_settings.h"
#include "desktop_pointer.h"
#include "display_config.h"
#include "input_defaults.h"
#include "imgui.h"

// Only desktop_settings_test redirects SDL calls. These wrappers call the real
// platform functions, so the fixture exercises actual handles and visibility.
#undef SDL_SetCursor
#undef SDL_ShowCursor
extern "C" void SDLCALL SDL_SetCursor(SDL_Cursor*);
extern "C" int SDLCALL SDL_ShowCursor(int);
namespace {
unsigned shapes=0,visibility=0,checks=0;
void require(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL: %s (shape=%u visibility=%u)\n",why,shapes,visibility);exit(1);}}
void reset(){shapes=visibility=0;}
}
extern "C" void SDLCALL gcn_test_set_cursor(SDL_Cursor* cursor){++shapes;SDL_SetCursor(cursor);}
extern "C" int SDLCALL gcn_test_show_cursor(int value){if(value!=SDL_QUERY)++visibility;return SDL_ShowCursor(value);}

void test_desktop_cursor(SDL_Window* window,void(*draw)()) {
    input_set_default("desktop_custom_cursor",1);
    SDL_setenv("GCN_POINTER_SCRIPT","cursor-fixture",1);
    desktop_pointer::context(true);
    desktop_pointer::publish("cursor-menu",{{0,0,.1f,.1f,7}});
    auto motion=[&](int x,int y){SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.x=x;e.motion.y=y;desktop_pointer::event(e,false);};
    auto tick=[&]{desktop_settings_update();draw();};
    auto steady=[&](const char* why){reset();for(int i=0;i<120;++i)tick();require(!shapes && !visibility,why);};
    auto f1=[&]{SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_SCANCODE_F1;require(desktop_settings_event(e),"F1 consumed");};

    motion(175,20);SDL_Delay(120);display_set_settings_hint(true);tick();
    auto* custom=SDL_GetCursor();
#ifdef _WIN32
    require(SDL_getenv("GCN_CURSOR_ASSET") && custom!=SDL_GetDefaultCursor(),"authored cursor fixture loaded");
#endif
    require(SDL_ShowCursor(SDL_QUERY)==SDL_ENABLE,"menu cursor visible");
    steady("stationary menu and F1 hint make no cursor mutations");
    require(SDL_GetCursor()==custom,"F1 hint preserves the authored cursor");
    display_set_settings_hint(false);steady("stationary menu without hint makes no cursor mutations");

    SDL_Event pad{};pad.type=SDL_CONTROLLERBUTTONDOWN;desktop_pointer::event(pad,false);
    reset();tick();require(visibility==1 && SDL_ShowCursor(SDL_QUERY)==SDL_DISABLE,"controller hides cursor once");
    display_set_settings_hint(true);steady("hint cannot reveal controller-hidden cursor");
    motion(175,20);reset();tick();require(visibility==1 && SDL_GetCursor()==custom,"mouse restores authored cursor once");
    steady("repeated mouse frames do not reassert cursor");
    motion(700,300);reset();tick();auto* idle=SDL_GetCursor();
#ifdef _WIN32
    require(shapes==1 && idle!=custom,"leaving button selects idle cursor once");
#endif
    steady("idle cursor does not reset each frame");
    motion(175,20);tick();SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.windowID=SDL_GetWindowID(window);click.button.button=SDL_BUTTON_LEFT;click.button.x=175;click.button.y=20;
    desktop_pointer::event(click,false);reset();tick();
    require(desktop_pointer::take()==7,"click action survives cursor fix");
#ifdef _WIN32
    require(shapes==1 && SDL_GetCursor()!=custom,"click selects tap cursor once");
#endif
    click.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(click,false);SDL_Delay(120);reset();tick();
#ifdef _WIN32
    require(shapes==1 && SDL_GetCursor()==custom,"tap returns to hover once");
#endif
    steady("hover remains stable after tap");

    desktop_pointer::context(false);reset();tick();require(visibility==1 && SDL_ShowCursor(SDL_QUERY)==SDL_DISABLE,"gameplay hides menu cursor once");
    steady("hint cannot reveal gameplay cursor");
    f1();require(desktop_settings_open(),"settings opens from gameplay");tick();
    require(SDL_ShowCursor(SDL_QUERY)==SDL_ENABLE,"settings owns a visible cursor");
    auto* settings_cursor=SDL_GetCursor();steady("open settings makes no redundant cursor mutations");
#ifdef _WIN32
    require(settings_cursor!=custom && settings_cursor!=idle,"settings uses its own cursor");
#endif
    // Reproduce a stale ImGui cache: another owner changes the native cursor.
    SDL_SetCursor(custom);reset();tick();
    require(SDL_GetCursor()==settings_cursor,"settings restores cursor after external ownership change");
    steady("restored settings cursor stays stable");
    f1();tick();require(!desktop_settings_open() && SDL_ShowCursor(SDL_QUERY)==SDL_DISABLE,"closing settings restores hidden gameplay cursor");
    desktop_pointer::context(true);desktop_pointer::publish("cursor-menu-return",{{0,0,.1f,.1f,7}});motion(175,20);tick();
    require(SDL_GetCursor()==custom && SDL_ShowCursor(SDL_QUERY)==SDL_ENABLE,"returning to menu restores authored hover cursor");
    f1();tick();require(SDL_GetCursor()==settings_cursor,"reopening settings replaces custom cursor despite cached shape");steady("reopened settings stays stable");
    f1();tick();
    printf("PASS: %u cursor checks; stationary title/hint/menu/settings each sustain 120 frames without native cursor mutations\n",checks);
}
