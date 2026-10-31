// Real SDL virtual controllers cover the physical trigger -> digital binding
// path on every port, with independent left/right settings and unchanged analog.
#include "desktop_controls.h"
#include "desktop_gamepad.h"
#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
using namespace desktop_controls;
namespace {
int checks=0;
void require(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL trigger: %s (%s)\n",why,SDL_GetError());exit(1);}}
bool exclude(int i){const char* name=SDL_JoystickNameForIndex(i);return !name || std::string(name).find("Trigger test ")!=0;}
void update(){SDL_JoystickUpdate();SDL_GameControllerUpdate();SDL_Event e;while(SDL_PollEvent(&e)){desktop_gamepad_event(e,SDL_GetTicks64());event(e);}}
std::array<PadState,4> sample_now(){update();std::array<PadState,4> pads;sample(pads,SDL_GetTicks64());return pads;}
struct Pad {
    SDL_Joystick* joy=nullptr;
    explicit Pad(int port){
        const auto name="Trigger test "+std::to_string(port);
        SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.vendor_id=0xf00d;desc.product_id=700+port;desc.name=name.c_str();
        const int index=SDL_JoystickAttachVirtualEx(&desc);require(index>=0,"attach virtual controller");
        char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid));
        const auto mapping=std::string(guid)+","+name+",a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,type:xboxone,";
        require(SDL_GameControllerAddMapping(mapping.c_str())>=0,"map virtual controller");
        joy=SDL_JoystickOpen(index);require(joy,"open virtual controller");axis(4,-32768);axis(5,-32768);update();desktop_gamepad_refresh(exclude);
    }
    void axis(int which,int value){require(SDL_JoystickSetVirtualAxis(joy,which,(Sint16)value)==0,"set virtual axis");}
    void trigger(int which,float fraction){axis(which,int(std::lround(-32768+65535*fraction)));}
    ~Pad(){const auto id=SDL_JoystickInstanceID(joy);SDL_JoystickClose(joy);for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_JoystickGetDeviceInstanceID(i)==id){SDL_JoystickDetachVirtual(i);break;}update();desktop_gamepad_refresh(exclude);}
};
std::string read_file(const std::filesystem::path& file){std::ifstream in(file);return {(std::istreambuf_iterator<char>(in)),{}};}
void write_file(const std::filesystem::path& file,const std::string& contents){std::ofstream(file,std::ios::trunc)<<contents;}
}
void test_trigger_click_points(const std::filesystem::path& file){
    require(!std::filesystem::exists(file),"fresh settings fixture");std::string error;require(load(file.string().c_str(),error),"missing settings default");
    for(const auto& p:configuration())require(p.trigger_left==.5f && p.trigger_right==.5f,"50 percent default on every port");
    Pad a(0),b(1),c(2),d(3);Pad* pads[]={&a,&b,&c,&d};const auto devices_now=devices();require(devices_now.size()==4,"four virtual trigger controllers");
    for(int p=0;p<4;++p)require(assign(p,Device::Controller,devices_now[p].id,devices_now[p].name),"assign trigger port");
    // Resolve each handle to its virtual device; enumeration order is not an
    // input-routing contract, even for controllers attached in this fixture.
    for(int p=0;p<4;++p)for(auto* candidate:{&a,&b,&c,&d})if(SDL_JoystickInstanceID(candidate->joy)==SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller(p))))pads[p]=candidate;
    sample_now();
    const float left[]={.1f,.5f,.99f,1.f},right[]={1.f,.99f,.5f,.1f};
    for(int p=0;p<4;++p){configuration()[p].trigger_left=left[p];configuration()[p].trigger_right=right[p];}
    for(int p=0;p<4;++p)for(int axis=4;axis<=5;++axis)for(float amount:{0.f,.09f,.1f,.11f,.49f,.5f,.51f,.98f,.99f,.995f,1.f}){
        pads[p]->trigger(axis,amount);const auto state=sample_now();
        const auto which=(SDL_GameControllerAxis)axis;
        const float actual=SDL_GameControllerGetAxis(controller(p),which)/32767.f;
        const float threshold=axis==4?left[p]:right[p];const auto bit=axis==4?PAD_L:PAD_R;
        require(bool(state[p].buttons&bit)==(actual>=threshold),"digital click uses this port and physical trigger's threshold");
        const auto analog=(uint8_t)((actual>.03f?actual:0.f)*255);
        require((axis==4?state[p].trig_l:state[p].trig_r)==analog,"click point cannot change analog travel");
        for(int other=0;other<4;++other)if(other!=p)require(!state[other].buttons && !state[other].trig_l && !state[other].trig_r,"trigger input stays on its assigned port");
        pads[p]->trigger(axis,0);sample_now();
    }
    // A physical trigger can drive any digital action, independently of L/R.
    auto& port=configuration()[0];port.trigger_left=.99f;port.trigger_right=.1f;
    port.bindings[LClick][0]={};port.bindings[RClick][0]={};
    port.bindings[Z][0]={Source::Axis,SDL_CONTROLLER_AXIS_TRIGGERLEFT,1};
    port.bindings[A][0]={Source::Axis,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,1};
    pads[0]->trigger(4,.75f);pads[0]->trigger(5,.75f);auto state=sample_now();
    require(state[0].buttons==PAD_A && state[0].trig_l>180 && state[0].trig_r>180,"remapped triggers honor independent points without raw L/R bypass");
    const auto analog_before=state[0].trig_l;port.trigger_left=.1f;state=sample_now();
    require(state[0].buttons==(PAD_A|PAD_Z) && state[0].trig_l==analog_before,"changing click point changes digital output only");
    port.trigger_left=.99f;port.keyboard=true;port.bindings[Z][1]={Source::Key,SDL_SCANCODE_SPACE,1};
    SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.scancode=SDL_SCANCODE_SPACE;event(key);
    require(sample_now()[0].buttons==(PAD_A|PAD_Z),"keyboard alternative bypasses trigger threshold");key.type=SDL_KEYUP;event(key);
    port.bindings[Y][0]={Source::Axis,SDL_CONTROLLER_AXIS_LEFTX,1};pads[0]->axis(0,20000);
    require((sample_now()[0].buttons&PAD_Y)!=0,"non-trigger axis digital threshold is unchanged");
    pads[0]->axis(0,0);pads[0]->trigger(4,0);pads[0]->trigger(5,0);sample_now();

    require(save(error),"save new trigger settings");const auto contents=read_file(file);
    require(contents.find("GCN_CONTROLLERS 3\n")==0 && contents.find("# trigger 0 ")!=std::string::npos,"settings retain old-reader-compatible version and comment extension");
    require(load(file.string().c_str(),error),"reload new settings");
    for(int p=0;p<4;++p)require(std::abs(configuration()[p].trigger_left-(p==0?.99f:left[p]))<.00001f && std::abs(configuration()[p].trigger_right-(p==0?.1f:right[p]))<.00001f,"independent port settings persist");
    const auto bindings=configuration()[0].bindings;
    std::istringstream input(contents);std::string line,legacy;
    while(std::getline(input,line))if(line.rfind("# trigger ",0)!=0)legacy+=line+'\n';
    for(int version:{1,2,3}){
        auto fixture=legacy;fixture.replace(0,17,"GCN_CONTROLLERS "+std::to_string(version));write_file(file,fixture);
        require(load(file.string().c_str(),error),"old controller format still loads");
        for(const auto& p:configuration())require(p.trigger_left==.5f && p.trigger_right==.5f,"old files get 50 percent defaults");
        require(configuration()[0].bindings[Z][0].source==bindings[Z][0].source && configuration()[0].bindings[Z][0].index==bindings[Z][0].index,"legacy load retains custom bindings");
    }
    write_file(file,"GCN_CONTROLLERS 3\n# trigger 0 0.1 1\n#\n# unrelated comment\n"+legacy.substr(legacy.find('\n')+1));
    require(load(file.string().c_str(),error) && configuration()[0].trigger_left==.1f && configuration()[0].trigger_right==1.f,"extension before port and empty comments preserve trigger points");
    write_file(file,legacy+"trigger 0 0.1 0.99\n");
    require(load(file.string().c_str(),error) && configuration()[0].trigger_left==.1f,"accept proposed plain trigger entry too");
    for(const char* bad:{"# trigger 0 0.09 0.5","# trigger 0 1.01 0.5","# trigger 0 0.5 -1","# trigger 4 0.5 0.5","# trigger -1 0.5 0.5","# trigger 0 nan 0.5","# trigger 0 0.5 inf","# trigger 0 0.5","# trigger 0 0.5 0.5 extra","# trigger 0 0.5 0.5\ntrigger 0 0.1 0.9"}){
        write_file(file,legacy+bad+'\n');require(!load(file.string().c_str(),error) && !error.empty(),"invalid or duplicate trigger settings rejected");
    }
    write_file(file,contents);require(load(file.string().c_str(),error),"restore valid trigger fixture");
    printf("PASS: %d trigger click-point checks across four SDL virtual controllers\n",checks);
}
