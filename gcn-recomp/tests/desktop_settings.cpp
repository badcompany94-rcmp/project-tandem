#define SDL_MAIN_HANDLED
#include "desktop_settings.h"
#include "desktop_controls.h"
#include "desktop_gamepad.h"
#include "desktop_display.h"
#include "desktop_sound.h"
#include "desktop_pointer.h"
#include "input_defaults.h"
#include "display_config.h"
#include "gx/gl.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
bool write_png(const char*,const uint8_t*,int,int);
bool pad_motor(int){return false;}
static void require(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s: %s\n",why,SDL_GetError());exit(1);}}
static void frame(){glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1280,720);glClearColor(.1f,.12f,.15f,1);glClear(GL_COLOR_BUFFER_BIT);desktop_settings_draw();glFinish();}
static void capture(const std::filesystem::path& path){std::vector<uint8_t> image(1280*720*4),flip(image.size());glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,image.data());for(int y=0;y<720;++y)std::copy_n(image.data()+(719-y)*1280*4,1280*4,flip.data()+y*1280*4);require(write_png(path.string().c_str(),flip.data(),1280,720),"capture UI");}
static Uint32 window_id;
static void click(int x,int y) {
 SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=window_id;e.motion.x=x;e.motion.y=y;desktop_settings_event(e);frame();
 e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=window_id;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;desktop_settings_event(e);frame();
 e.type=SDL_MOUSEBUTTONUP;desktop_settings_event(e);frame();frame();
}
static void key(int code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=(SDL_Scancode)code;desktop_settings_event(e);desktop_controls::event(e);e.type=SDL_KEYUP;desktop_settings_event(e);desktop_controls::event(e);}
int main(int argc,char** argv) {
    require(argc>=2,"output directory required");const std::filesystem::path out(argv[1]);require(!std::filesystem::exists(out),"fresh test directory");std::filesystem::create_directories(out);
    const auto cfg=(out/"controllers.cfg").string();SDL_SetMainReady();desktop_gamepad_configure();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0,"SDL initialize");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    auto* win=SDL_CreateWindow("Controller settings validation",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);require(win,"window");window_id=SDL_GetWindowID(win);auto ctx=SDL_GL_CreateContext(win);require(ctx,"GL context");require(gl_load_with(SDL_GL_GetProcAddress)>=33,"GL loader");
    SDL_setenv("GCN_INPUT_CONFIG",cfg.c_str(),1);desktop_gamepad_refresh();require(desktop_settings_init(win,ctx),"settings initialize");key(SDL_SCANCODE_F1);require(desktop_settings_open() && desktop_controls::blocked(),"F1 opens and captures input");
    frame();frame();capture(out/"ports.png");
    // The optional mode selects Mapping using real platform events. Coordinates
    // correspond to the visible tab in the deterministic 1280x720 layout.
    if(argc>2) {
        int x=atoi(argv[2]),y=argc>3?atoi(argv[3]):110;
        SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(win);e.motion.x=x;e.motion.y=y;desktop_settings_event(e);frame();
        e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(win);e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;desktop_settings_event(e);frame();
        e.type=SDL_MOUSEBUTTONUP;desktop_settings_event(e);frame();frame();capture(out/"mapping.png");
    }
    if(argc>4 && std::string(argv[4])=="sound") {
        require(desktop_sound::current().volume==25,"game startup volume is 25 percent");
        click(500,162);require(desktop_sound::current().volume>50,"Sound slider applies live");
        click(420,640);require(!desktop_settings_open() && desktop_sound::current().volume==25,"Discard restores opening volume");
        key(SDL_SCANCODE_F1);frame();frame();click(390,162);
        const auto chosen=desktop_sound::current();require(chosen.volume>30 && chosen.volume<70,"Sound slider new value");
        key(SDL_SCANCODE_ESCAPE);require(!desktop_settings_open(),"Sound closes with Escape");
        std::string error;require(desktop_sound::load((out/"sound.cfg").string(),25,error) && desktop_sound::current()==chosen,"closing persists sound");
        key(SDL_SCANCODE_F1);frame();capture(out/"sound-saved.png");
    }
    desktop_controls::configuration()[0].bindings[desktop_controls::A][1]={desktop_controls::Source::Key,SDL_SCANCODE_SPACE,1};
    std::string error;require(desktop_controls::save(error),"save live mappings");key(SDL_SCANCODE_ESCAPE);require(!desktop_settings_open(),"Escape closes settings without exiting");
    require(!desktop_gamepad_quit_requested(SDL_GetTicks64()+200,true),"closing settings does not request desktop exit");
    key(SDL_SCANCODE_F1);frame();require(desktop_controls::configuration()[0].bindings[desktop_controls::A][1].index==SDL_SCANCODE_SPACE,"saved mapping persists through reopen");
    key(SDL_SCANCODE_F1);require(!desktop_settings_open(),"F1 closes reopened settings");
    display_set_settings_hint(true);frame();capture(out/"hint.png");
    display_set_settings_hint(false);frame();capture(out/"no-hint.png");
    // Windowed resizing exercises the real GL context without switching monitors.
    auto next=desktop_display::current();next.width=960;next.height=540;
    const GLuint texture=[] {GLuint value;glGenTextures(1,&value);glBindTexture(GL_TEXTURE_2D,value);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);return value;}();
    require(desktop_display::preview(next,100,error),"real GL windowed resolution preview");
    require(glIsTexture(texture),"GL resources survive resolution change");
    require(desktop_display::tick(15100,error),"real GL windowed timeout reverts");
    int width,height;SDL_GetWindowSize(win,&width,&height);require(width==1280 && height==720,"real GL original size restored");
    glDeleteTextures(1,&texture);require(glGetError()==GL_NO_ERROR,"display/hint rendering has no GL errors");
    desktop_pointer::context(true);
    desktop_pointer::publish("test-menu",{{0,0,.1f,.1f,7}});
    SDL_Event pointer{};pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.windowID=window_id;pointer.button.button=SDL_BUTTON_LEFT;
    // A 4:3 menu occupies x=160..1120 in this 1280x720 window.
    pointer.button.x=175;pointer.button.y=20;desktop_pointer::event(pointer,false);
    require(desktop_pointer::take()==7,"pointer hits letterboxed menu using actual window size");
    pointer.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(pointer,false);
    pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.x=20;desktop_pointer::event(pointer,false);
    require(!desktop_pointer::take(),"pillarbox click cannot hit a menu option");
    pointer.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(pointer,false);
    pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.x=175;desktop_pointer::event(pointer,true);
    require(!desktop_pointer::take(),"settings intercept clicks before the game menu");
    pointer.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(pointer,false);
    pointer.type=SDL_MOUSEBUTTONDOWN;desktop_pointer::event(pointer,false);
    desktop_pointer::publish("next-menu",{{0,0,.1f,.1f,8}});
    require(!desktop_pointer::take(),"screen transition drops click intended for previous menu");
    pointer.type=SDL_MOUSEBUTTONUP;desktop_pointer::event(pointer,false);
    desktop_pointer::text_enabled=true;
    SDL_Event typing{};typing.type=SDL_TEXTINPUT;SDL_strlcpy(typing.text.text,"PLAYER7",sizeof(typing.text.text));desktop_pointer::event(typing,false);
    require(desktop_pointer::take_text()=="PLAYER7","text entry receives a full seven-character name");
    typing.type=SDL_KEYDOWN;typing.key.keysym.sym=SDLK_BACKSPACE;desktop_pointer::event(typing,false);
    require(desktop_pointer::take_text()=="\b","text entry receives editing command");desktop_pointer::text_enabled=false;
    if(SDL_getenv("GCN_CURSOR_ASSET")) {
        input_set_default("desktop_custom_cursor",1);pointer.type=SDL_MOUSEMOTION;pointer.motion.windowID=window_id;pointer.motion.x=175;pointer.motion.y=20;desktop_pointer::event(pointer,false);
        desktop_pointer::update(win,false);require(SDL_GetCursor()!=SDL_GetDefaultCursor(),"authored cursor decodes into a native SDL cursor");
    }
    desktop_pointer::context(false);
    key(SDL_SCANCODE_F1);frame();frame();click(580,640);frame();
    require(ImGui::GetTopMostPopupModal()!=nullptr,"desktop exit requires confirmation");capture(out/"exit-confirm.png");
    auto* modal=ImGui::FindWindowByName("Exit to desktop?");require(modal,"exit confirmation has a window");
    const auto pos=modal->Pos,size=modal->Size;
    SDL_Event quitting{};require(SDL_PeepEvents(&quitting,1,SDL_PEEKEVENT,SDL_QUIT,SDL_QUIT)==0,"opening exit confirmation does not quit");
    click((int)pos.x+65,(int)(pos.y+size.y-20));frame();
    require(ImGui::GetTopMostPopupModal()==nullptr && desktop_settings_open(),"Keep playing cancels desktop exit");
    click(580,640);frame();modal=ImGui::FindWindowByName("Exit to desktop?");require(modal,"exit confirmation reopens");
    click((int)(modal->Pos.x+modal->Size.x-55),(int)(modal->Pos.y+modal->Size.y-20));frame();
    require(SDL_PeepEvents(&quitting,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)==1,"confirmed desktop exit sends SDL_QUIT");
    desktop_settings_shutdown();desktop_gamepad_shutdown();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();printf("PASS: settings GL render, F1, Esc, input capture, persistence\n");return 0;
}
