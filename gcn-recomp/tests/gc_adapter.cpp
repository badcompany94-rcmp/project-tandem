#define SDL_MAIN_HANDLED
#include "desktop_gc_adapter.h"
#include "desktop_gamepad.h"
#include "desktop_controls.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks;
bool pad_motor(int){return false;}
static void require(bool value,const char* message){++checks;if(!value){fprintf(stderr,"FAIL %s: %s\n",message,SDL_GetError());exit(1);}}
static bool only_adapter(int i){const char* name=SDL_JoystickNameForIndex(i);return !name || strncmp(name,"GameCube USB Adapter ",21)!=0;}
static void pump(){SDL_JoystickUpdate();SDL_GameControllerUpdate();SDL_Event e;while(SDL_PollEvent(&e)){desktop_gamepad_event(e,0);desktop_controls::event(e);}desktop_gamepad_refresh(only_adapter);}
int main() {
    uint8_t bytes[37]={0x21};gc_adapter::Report report{};
    require(!gc_adapter::decode(nullptr,37,report),"null report rejected");
    for(int size=0;size<37;++size)require(!gc_adapter::decode(bytes,size,report),"truncated report rejected");
    bytes[0]=0x20;require(!gc_adapter::decode(bytes,37,report),"wrong report type rejected");bytes[0]=0x21;
    for(int p=0;p<4;++p){auto* s=bytes+1+9*p;s[0]=0x14;for(int a=0;a<4;++a)s[3+a]=128;}
    require(gc_adapter::decode(bytes,37,report),"four wired ports decoded");
    SDL_SetMainReady();desktop_gamepad_configure();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    require(SDL_Init(SDL_INIT_GAMECONTROLLER)==0,"SDL initialized");
    {
        gc_adapter::Pads adapter(1);adapter.update(report);pump();
        require(desktop_gamepad_count()==4,"four independently assignable controllers");
        desktop_controls::configuration()=desktop_controls::defaults();
        const auto devices=desktop_controls::devices();require(devices.size()==4,"F1 sees all adapter ports");
        for(int i=0;i<4;++i)require(desktop_controls::assign(i,desktop_controls::Device::Controller,devices[i].id,devices[i].name),"port assigned through normal controls");
        pump();
        for(int i=0;i<4;++i){auto* pad=desktop_gamepad_at(i);require(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)==0 && SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)==0,"neutral axes and triggers");}
        const struct {int byte,bit;SDL_GameControllerButton button;} buttons[]={
            {1,1,SDL_CONTROLLER_BUTTON_A},{1,2,SDL_CONTROLLER_BUTTON_B},{1,4,SDL_CONTROLLER_BUTTON_X},{1,8,SDL_CONTROLLER_BUTTON_Y},
            {1,16,SDL_CONTROLLER_BUTTON_DPAD_LEFT},{1,32,SDL_CONTROLLER_BUTTON_DPAD_RIGHT},{1,64,SDL_CONTROLLER_BUTTON_DPAD_DOWN},{1,128,SDL_CONTROLLER_BUTTON_DPAD_UP},
            {2,1,SDL_CONTROLLER_BUTTON_START},{2,2,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}};
        for(int port=0;port<4;++port)for(const auto& b:buttons) {
            bytes[1+9*port+b.byte]=b.bit;require(gc_adapter::decode(bytes,37,report),"button report decoded");adapter.update(report);pump();
            for(int other=0;other<4;++other)require(bool(SDL_GameControllerGetButton(desktop_gamepad_at(other),b.button))==(other==port),"buttons isolated between adapter ports");
            bytes[1+9*port+b.byte]=0;gc_adapter::decode(bytes,37,report);adapter.update(report);pump();
            require(!SDL_GameControllerGetButton(desktop_gamepad_at(port),b.button),"release propagated");
        }
        bytes[4]=228;bytes[5]=228;bytes[6]=28;bytes[7]=28;bytes[8]=100;bytes[9]=200;
        gc_adapter::decode(bytes,37,report);adapter.update(report);pump();auto* pad=desktop_gamepad_at(0);
        require(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)>30000 && SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY)<-30000,"main stick orientation");
        require(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX)<-30000 && SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTY)>30000,"C-stick orientation");
        require(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>12000 && SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)<14000,"analog trigger partial pull");
        bytes[3]=0x0c;gc_adapter::decode(bytes,37,report);adapter.update(report);pump();
        require(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)==32767 && SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)==32767,"physical trigger clicks produce full pull");
        require(SDL_GameControllerRumble(pad,0xffff,0,1000)==0,"rumble uses SDL controller path");
        require(adapter.rumble_packet()==std::array<uint8_t,5>{0x11,1,0,0,0},"rumble targets only selected powered port");
        bytes[1]=0x24;gc_adapter::decode(bytes,37,report);adapter.update(report);
        require(adapter.rumble_packet()[1]==0,"WaveBird never rumbles");
        bytes[1]=0x10;gc_adapter::decode(bytes,37,report);adapter.update(report);
        require(adapter.rumble_packet()[1]==0,"unpowered rumble suppressed");
        bytes[1]=0;gc_adapter::decode(bytes,37,report);adapter.update(report);pump();
        require(desktop_gamepad_count()==3,"one unplugged pad removes only its device");
        std::array<PadState,4> sampled;desktop_controls::sample(sampled,1000);
        require(!sampled[0].connected && sampled[0].buttons==0,"unplug releases assigned game input");
        bytes[1]=0x14;bytes[2]=0;bytes[3]=0;for(int a=0;a<4;++a)bytes[4+a]=128;bytes[8]=bytes[9]=0;
        gc_adapter::decode(bytes,37,report);adapter.update(report);pump();require(desktop_gamepad_count()==4,"reconnect restores fourth pad");
        adapter.clear();pump();require(desktop_gamepad_count()==0,"adapter unplug releases all ports");
    }
    desktop_controls::shutdown();desktop_gamepad_shutdown();SDL_Quit();
    printf("PASS %d GameCube report, SDL, four-port assignment, trigger, hotplug and rumble checks\n",checks);
}
