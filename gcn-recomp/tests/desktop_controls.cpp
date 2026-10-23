// Real SDL virtual devices exercise routing, remapping, persistence and hotplug.
#define SDL_MAIN_HANDLED
#include "desktop_controls.h"
#include "desktop_gamepad.h"
#include "desktop_pointer.h"
#include "input_defaults.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <SDL.h>
using namespace desktop_controls;
bool pad_motor(int){return false;}
static int checks;
static void require(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL: %s (%s)\n",why,SDL_GetError());exit(1);}}
static bool exclude(int i){return std::string(SDL_JoystickNameForIndex(i)).find("Port test ")!=0;}
static void update(){SDL_JoystickUpdate();SDL_GameControllerUpdate();SDL_Event e;while(SDL_PollEvent(&e)){desktop_gamepad_event(e,SDL_GetTicks64());event(e);}}
struct Pad {
    SDL_Joystick* joy;SDL_JoystickID id;
    explicit Pad(int number) {
        SDL_VirtualJoystickDesc d{};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;d.vendor_id=0xf00d;d.product_id=100+number;
        const auto name="Port test "+std::to_string(number);d.name=name.c_str();
        int i=SDL_JoystickAttachVirtualEx(&d);require(i>=0,"virtual attach");char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,33);
        const std::string map=std::string(guid)+","+name+",a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,type:xboxone,";
        require(SDL_GameControllerAddMapping(map.c_str())>=0,"virtual mapping");joy=SDL_JoystickOpen(i);require(joy,"virtual open");id=SDL_JoystickInstanceID(joy);
        axis(4,-32768);axis(5,-32768);update();desktop_gamepad_refresh(exclude);
    }
    void button(int b,bool down){require(SDL_JoystickSetVirtualButton(joy,b,down)==0,"virtual button");update();}
    void axis(int a,int v){require(SDL_JoystickSetVirtualAxis(joy,a,v)==0,"virtual axis");update();}
    void detach(){if(!joy)return;SDL_JoystickClose(joy);joy=nullptr;for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_JoystickGetDeviceInstanceID(i)==id){require(SDL_JoystickDetachVirtual(i)==0,"virtual detach");break;}update();desktop_gamepad_refresh(exclude);}
    ~Pad(){detach();}
};
static std::array<PadState,4> read(){update();std::array<PadState,4> out;sample(out,SDL_GetTicks64());return out;}
static void key(int code,bool down){SDL_Event e{};e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.scancode=(SDL_Scancode)code;event(e);}
int main(int argc,char** argv) {
    SDL_SetMainReady();desktop_gamepad_configure();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    require(SDL_Init(SDL_INIT_GAMECONTROLLER)==0,"SDL initialize");
    const auto file=std::filesystem::path(argc>1?argv[1]:"controller-test.cfg");
    require(!std::filesystem::exists(file),"test output must be fresh");std::string error;
    require(load(file.string().c_str(),error),"missing config uses defaults");
    {Pad a(0),b(1),c(2),d(3);const auto list=devices();require(list.size()==4,"four test devices");
    for(int p=0;p<4;++p)require(assign(p,Device::Controller,list[p].id,list[p].name),"assign distinct port");
    read();a.button(0,true);b.button(1,true);c.button(2,true);d.button(3,true);
    auto state=read();const int expected[]={PAD_A,PAD_B,PAD_X,PAD_Y};
    for(int p=0;p<4;++p)require(state[p].connected && state[p].buttons==expected[p],"four independent inputs without activity stealing");
    a.button(0,false);b.button(1,false);c.button(2,false);d.button(3,false);
    b.axis(0,32767);c.axis(1,-32768);state=read();
    require(state[1].stick_x==228 && state[2].stick_y==228 && state[0].stick_x==128 && state[3].stick_x==128,"independent sticks");
    b.axis(0,0);c.axis(1,0);
    configuration()[1].bindings[A][0]={Source::Button,SDL_CONTROLLER_BUTTON_Y,1};b.button(3,true);
    require((read()[1].buttons&PAD_A)!=0 && !read()[0].buttons,"remapped controller input reaches intended port");b.button(3,false);
    require(assign(0,Device::Controller,list[1].id,list[1].name),"move controller to another port");
    require(configuration()[1].device==Device::Disabled,"duplicate assignment disconnects old port");read();b.button(0,true);
    require(read()[0].buttons==PAD_A && !read()[1].connected,"moved device controls only new port");b.button(0,false);
    require(assign(0,Device::Automatic),"automatic port");read();c.button(2,true);
    require(read()[0].buttons==0 && read()[2].buttons==PAD_X,"auto cannot steal explicit controller");c.button(2,false);
    require(assign(1,Device::KeyboardMouse),"keyboard and mouse assignable to port two");read();
    key(SDL_SCANCODE_X,true);require(read()[1].buttons==PAD_A && read()[0].buttons==0,"keyboard routes only to chosen port");key(SDL_SCANCODE_X,false);
    configuration()[1].bindings[A][1]={Source::Key,SDL_SCANCODE_SPACE,1};key(SDL_SCANCODE_X,true);require(!read()[1].buttons,"old key no longer active");key(SDL_SCANCODE_X,false);
    key(SDL_SCANCODE_SPACE,true);require(read()[1].buttons==PAD_A,"new keyboard binding works");
    block(true);require(!read()[1].buttons,"settings suppress gameplay");block(false);require(!read()[1].buttons,"held input does not leak after menu closes");
    key(SDL_SCANCODE_SPACE,false);read();key(SDL_SCANCODE_SPACE,true);require(read()[1].buttons==PAD_A,"release rearms control");key(SDL_SCANCODE_SPACE,false);
    configuration()[1].bindings[B][1]={Source::MouseButton,SDL_BUTTON_LEFT,1};SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=1;event(click);
    require(read()[1].buttons==PAD_B,"mouse button maps to game control");click.type=SDL_MOUSEBUTTONUP;event(click);
    configuration()[1].bindings[StickLeft][1]={Source::MouseMotion,0,-1};configuration()[1].bindings[StickRight][1]={Source::MouseMotion,0,1};
    SDL_Event motion{};motion.type=SDL_MOUSEMOTION;motion.motion.xrel=40;event(motion);require(read()[1].stick_x>200,"mouse motion maps to stick");
    block(true);SDL_Delay(300);begin_capture(1,X,1,SDL_GetTicks64()-300);require(capture_ready(SDL_GetTicks64()),"capture arms after released inputs");
    key(SDL_SCANCODE_V,true);require(!capturing() && configuration()[1].bindings[X][1].index==SDL_SCANCODE_V,"keyboard capture commits binding");key(SDL_SCANCODE_V,false);
    require(save(error),"save config atomically");const auto saved=configuration();configuration()=defaults();require(load(file.string().c_str(),error),"reload config");
    require(configuration()[1].device==Device::KeyboardMouse && configuration()[1].bindings[A][1].index==SDL_SCANCODE_SPACE && configuration()[1].bindings[StickRight][1].source==Source::MouseMotion,"bindings and assignments persist");
    require(save(error),"replace existing config atomically");
    block(false);read();a.detach();require(read()[0].connected,"automatic can use remaining unreserved controller");
    set_focused(false);read();c.button(0,true);require(read()[2].buttons==PAD_A,"background gamepad behavior is preserved");c.button(0,false);
    key(SDL_SCANCODE_SPACE,true);require(!read()[1].buttons,"unfocused keyboard does not control the game");key(SDL_SCANCODE_SPACE,false);set_focused(true);read();
    c.button(0,true);c.axis(0,18000);
    require(read()[2].buttons==PAD_A,"held controller is active before focus changes");
    set_focused(false);state=read();
    require(state[2].buttons==PAD_A && state[2].stick_x>128,"focus loss cannot swallow held controller input");
    set_focused(true);state=read();
    require(state[2].buttons==PAD_A && state[2].stick_x>128,"focus regain cannot require controller release");
    c.button(0,false);c.axis(0,0);read();
    d.button(0,true);require(read()[3].buttons==PAD_A,"assigned controller active before disconnect");d.detach();require(!read()[3].connected && !read()[3].buttons,"disconnect clears only assigned port");
    {Pad replug(3);read();replug.button(0,true);require(read()[3].connected && read()[3].buttons==PAD_A,"same persistent device reconnects to its port");replug.button(0,false);}
    {Pad twin1(20),twin2(20);std::string one,two;
    for(const auto& dev:devices()){const auto instance=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(dev.handle));if(instance==twin1.id)one=dev.id;if(instance==twin2.id)two=dev.id;}
    require(!one.empty() && !two.empty() && one!=two,"identical controllers have distinct identities");
    assign(2,Device::Controller,one,"Twin 1");assign(3,Device::Controller,two,"Twin 2");read();
    twin1.detach();twin2.button(0,true);state=read();require(!state[2].connected && state[3].buttons==PAD_A,"disconnecting first identical pad cannot transfer the second to its port");twin2.button(0,false);}
    std::ofstream broken(file,std::ios::trunc);broken<<"GCN_CONTROLLERS 999\n";broken.close();require(!load(file.string().c_str(),error) && !error.empty(),"invalid config reported, defaults retained");
    require(configuration()[0].device==Device::Automatic,"safe defaults after invalid file");
    input_set_default("desktop_wasd_mouse",1);configuration()=defaults();read();
    // Use the same real device and event path across menus and racing. A held
    // menu click must not become an item use when the race context activates.
    desktop_pointer::context(true);read();
    key(SDL_SCANCODE_RETURN,true);require(read()[0].buttons==PAD_A,"menu Enter confirms without pausing");key(SDL_SCANCODE_RETURN,false);
    key(SDL_SCANCODE_W,true);require(!read()[0].buttons,"racing accelerator does not activate a menu item");key(SDL_SCANCODE_W,false);
    click.type=SDL_MOUSEBUTTONDOWN;event(click);require(!read()[0].buttons,"menu click does not use an item");
    desktop_pointer::context(false);require(!read()[0].buttons,"held menu click is consumed at race transition");
    click.type=SDL_MOUSEBUTTONUP;event(click);read();click.type=SDL_MOUSEBUTTONDOWN;event(click);
    require(read()[0].buttons==PAD_X,"new racing click uses item");click.type=SDL_MOUSEBUTTONUP;event(click);
    key(SDL_SCANCODE_W,true);key(SDL_SCANCODE_A,true);state=read();
    require(state[0].buttons==PAD_A && state[0].stick_x<128,"W accelerates while A steers left");
    key(SDL_SCANCODE_W,false);key(SDL_SCANCODE_A,false);key(SDL_SCANCODE_SPACE,true);state=read();
    require((state[0].buttons&PAD_R) && state[0].trig_r==255,"Space engages drift trigger");key(SDL_SCANCODE_SPACE,false);
    key(SDL_SCANCODE_Q,true);require(read()[0].stick_y>200,"Q pushes the main stick up");key(SDL_SCANCODE_Q,false);
    key(SDL_SCANCODE_E,true);require(read()[0].stick_y<60,"E pushes the main stick down");key(SDL_SCANCODE_E,false);
    input_set_default("desktop_escape_back",1);desktop_pointer::context(true);read();key(SDL_SCANCODE_ESCAPE,true);
    require(!read()[0].buttons,"menu Escape is owned by the pointer event queue");key(SDL_SCANCODE_ESCAPE,false);
    read();key(SDL_SCANCODE_ESCAPE,true);require(!read()[0].buttons,"Escape cannot also arrive as a sampled pad button");
    desktop_pointer::publish("confirmation",{});require(!read()[0].buttons,"held Escape cannot dismiss a newly opened confirmation");
    key(SDL_SCANCODE_ESCAPE,false);read();key(SDL_SCANCODE_ESCAPE,true);require(!read()[0].buttons,"a second menu Escape still uses only the event path");key(SDL_SCANCODE_ESCAPE,false);
    SDL_Event escape{};escape.type=SDL_KEYDOWN;escape.key.keysym.scancode=SDL_SCANCODE_ESCAPE;desktop_gamepad_event(escape,1000);
    require(!desktop_gamepad_quit_requested(2000,true),"Escape-back profile never closes the game");
    desktop_pointer::context(false);read();key(SDL_SCANCODE_ESCAPE,true);require(read()[0].buttons==PAD_START,"Escape opens pause during racing");key(SDL_SCANCODE_ESCAPE,false);
    desktop_pointer::navigation_enabled=true;key(SDL_SCANCODE_W,true);require(read()[0].buttons==PAD_A,"race accelerator remains confirm throughout results");key(SDL_SCANCODE_W,false);
    key(SDL_SCANCODE_DOWN,true);require(read()[0].stick_y<60,"arrows navigate native results using its main-stick path");key(SDL_SCANCODE_DOWN,false);
    key(SDL_SCANCODE_ESCAPE,true);require(read()[0].buttons==PAD_B,"Escape goes back inside results and pause");key(SDL_SCANCODE_ESCAPE,false);
    desktop_pointer::navigation_enabled=false;
    b.button(0,true);require(read()[0].buttons==PAD_A,"controller works immediately after mouse and keyboard");b.button(0,false);
    desktop_pointer::context(true);read();desktop_pointer::text_enabled=true;
    key(SDL_SCANCODE_X,true);key(SDL_SCANCODE_Y,true);require(!read()[0].buttons,"typing names does not ready or start a room");
    key(SDL_SCANCODE_X,false);key(SDL_SCANCODE_Y,false);desktop_pointer::text_enabled=false;
    configuration()[0].bindings[A][1]={Source::Key,SDL_SCANCODE_V,1};
    key(SDL_SCANCODE_RETURN,true);require(read()[0].buttons==PAD_A,"rebinding race accelerator preserves menu confirm");key(SDL_SCANCODE_RETURN,false);
    desktop_pointer::context(false);read();key(SDL_SCANCODE_V,true);require(read()[0].buttons==PAD_A,"custom accelerator works in race context");key(SDL_SCANCODE_V,false);
    require(assign(1,Device::KeyboardMouse),"move keyboard to second player with new preset");read();key(SDL_SCANCODE_W,true);
    state=read();require(state[1].buttons==PAD_A && !state[0].buttons,"new preset respects explicit local keyboard assignment");key(SDL_SCANCODE_W,false);
    input_set_default("desktop_wasd_mouse",0);configuration()=defaults();
    configuration()[0].bindings[A][0]={Source::Button,SDL_CONTROLLER_BUTTON_Y,1};
    configuration()[1].bindings[A][1]={Source::Key,SDL_SCANCODE_V,1};
    require(save(error),"write migration fixture");
    {std::ifstream old(file);std::string contents((std::istreambuf_iterator<char>(old)),{});old.close();
     contents.replace(0,std::string("GCN_CONTROLLERS 2").size(),"GCN_CONTROLLERS 1");std::ofstream(file,std::ios::trunc)<<contents;}
    input_set_default("desktop_wasd_mouse",1);require(load(file.string().c_str(),error),"load previous release mapping");
    require(configuration()[0].bindings[A][1].index==SDL_SCANCODE_W && configuration()[0].bindings[X][1].source==Source::MouseButton,"untouched previous keyboard defaults migrate");
    require(configuration()[0].bindings[A][0].index==SDL_CONTROLLER_BUTTON_Y,"keyboard migration preserves custom controller mapping");
    require(configuration()[1].bindings[A][1].index==SDL_SCANCODE_V,"keyboard migration preserves custom keyboard mapping");
    require(save(error) && load(file.string().c_str(),error),"migrated settings save and reload");
    // Upgrade the previous mouse/item preset without touching controller or
    // custom keyboard bindings. Exercise actual sampled SDL events afterward.
    configuration()=defaults();
    configuration()[0].bindings[A][0]={Source::Button,SDL_CONTROLLER_BUTTON_Y,1};
    configuration()[1].bindings[A][1]={Source::Key,SDL_SCANCODE_V,1};
    require(save(error),"save previous mouse preset fixture");
    {std::ifstream old(file);std::string contents((std::istreambuf_iterator<char>(old)),{});old.close();
     contents.replace(0,std::string("GCN_CONTROLLERS 3").size(),"GCN_CONTROLLERS 2");std::ofstream(file,std::ios::trunc)<<contents;}
    input_set_default("desktop_mouse_drive",1);
    require(load(file.string().c_str(),error),"migrate mouse driving preset");
    require(configuration()[0].bindings[A][1].source==Source::MouseButton && configuration()[0].bindings[A][1].index==SDL_BUTTON_LEFT,"previous defaults become mouse accelerator");
    require(configuration()[0].bindings[A][0].index==SDL_CONTROLLER_BUTTON_Y,"new preset preserves controller remapping");
    require(configuration()[1].bindings[A][1].index==SDL_SCANCODE_V,"new preset preserves customized keyboard profile");
    require(save(error) && load(file.string().c_str(),error),"new preset survives save/reload");
    desktop_pointer::context(true);read();
    click.button.button=SDL_BUTTON_LEFT;click.type=SDL_MOUSEBUTTONDOWN;event(click);
    require(!read()[0].buttons,"menu click does not leak mouse accelerator");
    desktop_pointer::context(false);require(!read()[0].buttons,"held menu click blocked on race entry");
    click.type=SDL_MOUSEBUTTONUP;event(click);read();click.type=SDL_MOUSEBUTTONDOWN;event(click);
    key(SDL_SCANCODE_A,true);state=read();
    require(state[0].buttons==PAD_A && state[0].stick_x<128,"left click accelerates while A steers");
    key(SDL_SCANCODE_A,false);click.type=SDL_MOUSEBUTTONUP;event(click);
    click.button.button=SDL_BUTTON_RIGHT;click.type=SDL_MOUSEBUTTONDOWN;event(click);
    require(read()[0].buttons==PAD_B,"right click brakes");click.type=SDL_MOUSEBUTTONUP;event(click);
    key(SDL_SCANCODE_W,true);state=read();require(state[0].stick_y>200 && !state[0].buttons,"W is stick up only");key(SDL_SCANCODE_W,false);
    key(SDL_SCANCODE_S,true);state=read();require(state[0].stick_y<60 && !state[0].buttons,"S is stick down only");key(SDL_SCANCODE_S,false);
    for(auto [code,button]:{std::pair{SDL_SCANCODE_SPACE,PAD_X},std::pair{SDL_SCANCODE_Q,PAD_Y},std::pair{SDL_SCANCODE_E,PAD_Z}}) {
        key(code,true);state=read();require(state[0].buttons==button && state[0].stick_y==128 && !state[0].trig_r,"item/switch keys have no stale stick or drift action");key(code,false);
    }
    key(SDL_SCANCODE_LSHIFT,true);state=read();require(state[0].buttons==PAD_R && state[0].trig_r==255,"left Shift engages drift only");key(SDL_SCANCODE_LSHIFT,false);
    key(SDL_SCANCODE_RSHIFT,true);require(!read()[0].buttons,"right Shift no longer switches riders");key(SDL_SCANCODE_RSHIFT,false);
    key(SDL_SCANCODE_LCTRL,true);state=read();require(state[0].buttons==PAD_L && state[0].trig_l==255,"Ctrl remains left trigger");key(SDL_SCANCODE_LCTRL,false);
    desktop_pointer::navigation_enabled=true;click.button.button=SDL_BUTTON_LEFT;click.type=SDL_MOUSEBUTTONDOWN;event(click);
    require(read()[0].buttons==PAD_A,"mouse accelerator confirms between races");click.type=SDL_MOUSEBUTTONUP;event(click);
    desktop_pointer::navigation_enabled=false;desktop_pointer::context(true);read();
    key(SDL_SCANCODE_RETURN,true);require(read()[0].buttons==PAD_A,"menu Enter unaffected by new racing preset");key(SDL_SCANCODE_RETURN,false);
    }
    shutdown();desktop_gamepad_shutdown();SDL_Quit();printf("PASS: %d desktop port/mapping checks\n",checks);return 0;
}
