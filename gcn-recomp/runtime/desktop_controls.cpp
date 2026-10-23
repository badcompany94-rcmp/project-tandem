#include "desktop_controls.h"
#include "desktop_gamepad.h"
#include "input_defaults.h"
#include "desktop_pointer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace desktop_controls {
namespace {
Configuration config;
std::string config_path;
bool initialized=false,ui_block=false,release_guard=false,focused=true;
std::array<bool,SDL_NUM_SCANCODES> keys{};
uint32_t mouse_buttons=0;
uint32_t guarded_mouse=0;
unsigned input_generation=0;
std::array<bool,SDL_NUM_SCANCODES> guarded_keys{};
float mouse_x=0,mouse_y=0;
int wheel=0;
uint64_t last_sample=0,wheel_at=0;
std::array<PadState,4> last_preview{};
std::array<bool,4> rumbling{};
std::map<SDL_JoystickID,std::string> device_ids;
struct Capture {int port=-1,action=0,alternative=0;uint64_t started=0;bool ready=false;} capture;
const char* const names[]={"A","B","X","Y","Z","Start / Pause","D-pad Up","D-pad Down","D-pad Left","D-pad Right",
    "L click","R click","L analog","R analog","Stick Left","Stick Right","Stick Up","Stick Down",
    "C-stick Left","C-stick Right","C-stick Up","C-stick Down"};
const uint16_t bits[]={PAD_A,PAD_B,PAD_X,PAD_Y,PAD_Z,PAD_START,PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,PAD_L,PAD_R};
bool valid_port(int p){return p>=0 && p<4;}
bool controller_source(Source source){return source==Source::Button || source==Source::Axis;}
bool valid_binding(const Binding& b) {
    if(b.sign!=1 && b.sign!=-1)return false;
    switch(b.source) {
    case Source::None:return b.index==0;
    case Source::Key:return b.index>SDL_SCANCODE_UNKNOWN && b.index<SDL_NUM_SCANCODES &&
        b.index!=SDL_SCANCODE_ESCAPE && b.index!=SDL_SCANCODE_F1 && b.index!=SDL_SCANCODE_F10 && b.index!=SDL_SCANCODE_F11;
    case Source::MouseButton:return b.index>=1 && b.index<=5;
    case Source::MouseWheel:return b.index==0;
    case Source::MouseMotion:return b.index==0 || b.index==1;
    case Source::Button:return b.index>=0 && b.index<SDL_CONTROLLER_BUTTON_MAX;
    case Source::Axis:return b.index>=0 && b.index<SDL_CONTROLLER_AXIS_MAX;
    }return false;
}
bool any_controller_pressed(SDL_GameController* pad) {
    if(!pad)return false;
    for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i)if(SDL_GameControllerGetButton(pad,(SDL_GameControllerButton)i))return true;
    for(int i=0;i<SDL_CONTROLLER_AXIS_MAX;++i) {
        const int v=SDL_GameControllerGetAxis(pad,(SDL_GameControllerAxis)i);
        if(i<SDL_CONTROLLER_AXIS_TRIGGERLEFT?std::abs(v)>8000:v>4000)return true;
    }return false;
}
bool neutral() {
    for(int i=0;i<SDL_NUM_SCANCODES;++i)if(i!=SDL_SCANCODE_F1 && i!=SDL_SCANCODE_ESCAPE && keys[i])return false;
    if(mouse_buttons)return false;
    for(int i=0;i<desktop_gamepad_count();++i)if(any_controller_pressed(desktop_gamepad_at(i)))return false;
    return true;
}
void captured(Binding b) {
    if(!capturing() || !valid_binding(b))return;
    configuration()[capture.port].bindings[capture.action][capture.alternative]=b;
    capture={};
}
float value(const Binding& b,SDL_GameController* pad,bool keyboard,const Port& port) {
    switch(b.source) {
    case Source::Key:
        if(!keyboard)return 0;
        if(b.index==SDL_SCANCODE_RETURN)return desktop_gamepad_keyboard_start(keys[b.index] && !guarded_keys[b.index],false)?1:0;
        return keys[b.index] && !guarded_keys[b.index]?1:0;
    case Source::MouseButton:return keyboard && (mouse_buttons&~guarded_mouse&SDL_BUTTON(b.index))?1:0;
    case Source::MouseWheel:return keyboard && wheel*b.sign>0?1:0;
    case Source::MouseMotion:return keyboard?std::clamp((b.index==0?mouse_x:mouse_y)*b.sign,0.f,1.f):0;
    case Source::Button:return pad && SDL_GameControllerGetButton(pad,(SDL_GameControllerButton)b.index)?1:0;
    case Source::Axis:
        if(pad) {
            float v=SDL_GameControllerGetAxis(pad,(SDL_GameControllerAxis)b.index)/32767.f*b.sign;
            if(b.index<SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                if(v<=port.deadzone)return 0;
                // Keep existing full travel while providing an adjustable resting zone.
                return std::clamp(v,0.f,1.f);
            }
            return v>.03f?std::clamp(v,0.f,1.f):0;
        }return 0;
    default:return 0;
    }
}
void stop_rumble() {
    for(int i=0;i<desktop_gamepad_count();++i)SDL_GameControllerRumble(desktop_gamepad_at(i),0,0,0);
    rumbling.fill(false);
}
}

const char* action_name(int a){return a>=0 && a<ActionCount?names[a]:"Unknown";}
static Configuration defaults_for_keyboard(bool wasd_mouse,bool mouse_drive=false) {
    Configuration c{};
    for(auto& p:c) {
        const int buttons[]={SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN,SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
        const int keyboard[]={SDL_SCANCODE_X,SDL_SCANCODE_Z,SDL_SCANCODE_C,SDL_SCANCODE_S,SDL_SCANCODE_D,SDL_SCANCODE_RETURN,
            SDL_SCANCODE_I,SDL_SCANCODE_K,SDL_SCANCODE_J,SDL_SCANCODE_L,SDL_SCANCODE_Q,SDL_SCANCODE_W,SDL_SCANCODE_Q,SDL_SCANCODE_W,
            SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN};
        for(int a=0;a<10;++a)p.bindings[a][0]={Source::Button,buttons[a],1};
        for(int a=0;a<18;++a)p.bindings[a][1]={Source::Key,keyboard[a],1};
        p.bindings[Start][2]={Source::Key,SDL_SCANCODE_TAB,1};
        if(wasd_mouse) {
            for(auto& action:p.bindings){action[1]={};action[2]={};}
            const std::pair<Action,int> keys[]={{A,SDL_SCANCODE_W},{B,SDL_SCANCODE_S},
                {Z,SDL_SCANCODE_LSHIFT},{Start,SDL_SCANCODE_RETURN},
                {DUp,SDL_SCANCODE_UP},{DDown,SDL_SCANCODE_DOWN},{DLeft,SDL_SCANCODE_LEFT},{DRight,SDL_SCANCODE_RIGHT},
                {LClick,SDL_SCANCODE_LCTRL},{LAnalog,SDL_SCANCODE_LCTRL},
                {RClick,SDL_SCANCODE_SPACE},{RAnalog,SDL_SCANCODE_SPACE},
                {StickLeft,SDL_SCANCODE_A},{StickRight,SDL_SCANCODE_D},{StickUp,SDL_SCANCODE_Q},{StickDown,SDL_SCANCODE_E}};
            for(auto [action,key]:keys)p.bindings[action][1]={Source::Key,key,1};
            p.bindings[X][1]={Source::MouseButton,SDL_BUTTON_LEFT,1};
            p.bindings[Y][1]={Source::MouseButton,SDL_BUTTON_RIGHT,1};
            p.bindings[Z][2]={Source::Key,SDL_SCANCODE_RSHIFT,1};
            p.bindings[LClick][2]=p.bindings[LAnalog][2]={Source::Key,SDL_SCANCODE_RCTRL,1};
            if(mouse_drive) {
                p.bindings[A][1]={Source::MouseButton,SDL_BUTTON_LEFT,1};
                p.bindings[B][1]={Source::MouseButton,SDL_BUTTON_RIGHT,1};
                p.bindings[X][1]={Source::Key,SDL_SCANCODE_SPACE,1};
                p.bindings[Y][1]={Source::Key,SDL_SCANCODE_Q,1};
                p.bindings[Z][1]={Source::Key,SDL_SCANCODE_E,1};
                p.bindings[Z][2]={};
                p.bindings[RClick][1]=p.bindings[RAnalog][1]={Source::Key,SDL_SCANCODE_LSHIFT,1};
                p.bindings[StickUp][1]={Source::Key,SDL_SCANCODE_W,1};
                p.bindings[StickDown][1]={Source::Key,SDL_SCANCODE_S,1};
            }
        }
        p.bindings[LClick][0]=p.bindings[LAnalog][0]={Source::Axis,SDL_CONTROLLER_AXIS_TRIGGERLEFT,1};
        p.bindings[RClick][0]=p.bindings[RAnalog][0]={Source::Axis,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,1};
        for(int i=0;i<8;++i)p.bindings[StickLeft+i][0]={Source::Axis,i<4?(i<2?SDL_CONTROLLER_AXIS_LEFTX:SDL_CONTROLLER_AXIS_LEFTY):
            (i<6?SDL_CONTROLLER_AXIS_RIGHTX:SDL_CONTROLLER_AXIS_RIGHTY),i%2==0?-1:1};
    }
    c[0].device=Device::Automatic;c[0].keyboard=true;
    return c;
}
Configuration defaults(){return defaults_for_keyboard(input_default("desktop_wasd_mouse",0)!=0,input_default("desktop_mouse_drive",0)!=0);}
Configuration& configuration(){if(!initialized){config=defaults();initialized=true;}return config;}
const std::string& path(){return config_path;}

std::vector<DeviceInfo> devices() {
    std::vector<DeviceInfo> out;
    for(int i=0;i<desktop_gamepad_count();++i) {
        auto* pad=desktop_gamepad_at(i);auto* joy=SDL_GameControllerGetJoystick(pad);
        char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy),guid,sizeof(guid));
        const char* serial=SDL_JoystickGetSerial(joy);
        const char* device_path=SDL_JoystickPath(joy);
        std::string base=std::string(guid)+":"+(serial && *serial?std::string("serial=")+serial:
            device_path && *device_path?std::string("path=")+device_path:"");
        const auto instance=SDL_JoystickInstanceID(joy);
        auto remembered=device_ids.find(instance);
        std::string id;
        if(remembered!=device_ids.end())id=remembered->second;
        else {
            int ordinal=1;
            for(;;++ordinal) {
                bool used=false;const auto candidate=base+":"+std::to_string(ordinal);
                for(int j=0;j<desktop_gamepad_count();++j) {
                    auto* other=SDL_GameControllerGetJoystick(desktop_gamepad_at(j));
                    const auto found=device_ids.find(SDL_JoystickInstanceID(other));
                    if(found!=device_ids.end() && found->second==candidate)used=true;
                }
                if(!used){id=candidate;break;}
            }
            device_ids[instance]=id;
        }
        int occurrence=1;
        for(const auto& other:out)if(other.name==SDL_GameControllerName(pad) || other.name.find(std::string(SDL_GameControllerName(pad))+" #")==0)++occurrence;
        const char* name=SDL_GameControllerName(pad);
        std::string label=name?name:"Controller";
        if(occurrence>1)label+=" #"+std::to_string(occurrence);
        out.push_back({id,label,pad});
    }return out;
}
SDL_GameController* controller(int port) {
    if(!valid_port(port))return nullptr;
    auto& c=configuration();const auto d=devices();
    if(c[port].device==Device::Controller) {
        for(const auto& item:d)if(item.id==c[port].controller_id)return item.handle;
    }else if(c[port].device==Device::Automatic) {
        auto reserved=[&](const DeviceInfo& item){for(int i=0;i<4;++i)if(i!=port && c[i].device==Device::Controller && c[i].controller_id==item.id)return true;return false;};
        for(const auto& item:d)if(item.handle==desktop_gamepad_active() && !reserved(item))return item.handle;
        for(const auto& item:d)if(!reserved(item))return item.handle;
    }return nullptr;
}
bool assign(int port,Device type,const std::string& id,const std::string& name) {
    if(!valid_port(port) || (type==Device::Controller && id.empty()))return false;
    auto& c=configuration();stop_rumble();
    for(int i=0;i<4;++i)if(i!=port) {
        if((type==Device::Controller && c[i].device==Device::Controller && c[i].controller_id==id) ||
           (type==Device::Automatic && c[i].device==Device::Automatic) ||
           (type==Device::KeyboardMouse && c[i].device==Device::KeyboardMouse)){c[i].device=Device::Disabled;c[i].keyboard=false;}
        if(type==Device::KeyboardMouse)c[i].keyboard=false;
    }
    c[port].device=type;c[port].controller_id=id;c[port].controller_name=name;
    c[port].keyboard=type==Device::KeyboardMouse || (type==Device::Automatic && port==0);
    if(c[port].keyboard)for(int i=0;i<4;++i)if(i!=port){c[i].keyboard=false;if(c[i].device==Device::KeyboardMouse)c[i].device=Device::Disabled;}
    release_guard=true;return true;
}

std::string binding_name(const Binding& b) {
    switch(b.source) {
    case Source::Key:return SDL_GetScancodeName((SDL_Scancode)b.index);
    case Source::MouseButton:return b.index==1?"Mouse Left":b.index==2?"Mouse Middle":b.index==3?"Mouse Right":b.index==4?"Mouse X1":"Mouse X2";
    case Source::MouseWheel:return b.sign>0?"Wheel Up":"Wheel Down";
    case Source::MouseMotion:return std::string("Mouse ")+(b.index==0?(b.sign>0?"Right":"Left"):(b.sign>0?"Down":"Up"));
    case Source::Button:{const char* name=SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b.index);return name?name:"Button";}
    case Source::Axis:{const char* name=SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)b.index);return std::string(name?name:"Axis")+(b.sign>0?" +":" -");}
    default:return "Unbound";
    }
}
bool load(const char* file,std::string& error) {
    error.clear();config_path=file?file:"saves/controllers.cfg";
    configuration()=defaults();
    std::error_code ec;
    if(!std::filesystem::exists(config_path,ec))return !ec;
    if(ec || std::filesystem::file_size(config_path,ec)>65536){error="Controller settings file is too large or unreadable.";return false;}
    std::ifstream in(config_path);std::string tag;int version;
    if(!(in>>tag>>version) || tag!="GCN_CONTROLLERS" || version<1 || version>3){error="Unsupported controller settings format.";return false;}
    auto next=defaults();std::array<bool,4> seen{};std::string line;std::getline(in,line);
    while(std::getline(in,line)) {
        if(line.empty() || line[0]=='#')continue;
        std::istringstream row(line);row>>tag;int p;
        if(tag=="port") {
            int device,keyboard;Port data;
            if(!(row>>p>>device>>keyboard>>data.deadzone>>data.mouse_sensitivity>>std::quoted(data.controller_id)>>std::quoted(data.controller_name)) ||
               !valid_port(p) || seen[p] || device<0 || device>3 || keyboard<0 || keyboard>1 ||
               !std::isfinite(data.deadzone) || data.deadzone<.02f || data.deadzone>.5f ||
               !std::isfinite(data.mouse_sensitivity) || data.mouse_sensitivity<.001f || data.mouse_sensitivity>.2f ||
               (device==(int)Device::Controller && data.controller_id.empty())){error="Invalid controller port settings.";return false;}
            data.device=(Device)device;data.keyboard=keyboard!=0;data.bindings=next[p].bindings;next[p]=data;seen[p]=true;
        }else if(tag=="bind") {
            int a,slot,source;Binding b;
            if(!(row>>p>>a>>slot>>source>>b.index>>b.sign) || !valid_port(p) || a<0 || a>=ActionCount || slot<0 || slot>2 || source<0 || source>6){error="Invalid controller binding.";return false;}
            b.source=(Source)source;
            if(!valid_binding(b) || (slot==0 && b.source!=Source::None && !controller_source(b.source)) || (slot>0 && controller_source(b.source))){error="Invalid controller input source.";return false;}
            next[p].bindings[a][slot]=b;
        }else {error="Unknown controller settings entry.";return false;}
    }
    for(int p=0;p<4;++p) {
        if(!seen[p]){error="Controller settings are incomplete.";return false;}
        for(int q=0;q<p;++q) {
            if((next[p].device==Device::Controller && next[q].device==Device::Controller && next[p].controller_id==next[q].controller_id) ||
               (next[p].device==Device::Automatic && next[q].device==Device::Automatic) ||
               (next[p].keyboard && next[q].keyboard)){error="An input device is assigned to more than one port.";return false;}
        }
    }
    if(version==1 && input_default("desktop_wasd_mouse",0)!=0) {
        const auto legacy=defaults_for_keyboard(false),updated=defaults_for_keyboard(true);
        for(int p=0;p<4;++p) {
            bool unchanged=true;
            for(int a=0;a<ActionCount;++a)for(int s=1;s<3;++s) {
                const auto& actual=next[p].bindings[a][s];const auto& old=legacy[p].bindings[a][s];
                unchanged&=actual.source==old.source && actual.index==old.index && actual.sign==old.sign;
            }
            if(unchanged)for(int a=0;a<ActionCount;++a)for(int s=1;s<3;++s)next[p].bindings[a][s]=updated[p].bindings[a][s];
        }
    }
    if(version==2 && input_default("desktop_wasd_mouse",0)!=0)for(auto& port:next) {
        // Previously unassigned vertical directions adopt the new defaults;
        // existing user mappings remain authoritative.
        for(auto [action,key]:{std::pair{StickUp,SDL_SCANCODE_Q},std::pair{StickDown,SDL_SCANCODE_E}})
            if(port.bindings[action][1].source==Source::None && port.bindings[action][2].source==Source::None)
                port.bindings[action][1]={Source::Key,key,1};
    }
    if(version<3 && input_default("desktop_wasd_mouse",0)!=0 && input_default("desktop_mouse_drive",0)!=0) {
        const auto previous=defaults_for_keyboard(true),updated=defaults();
        for(int p=0;p<4;++p) {
            bool unchanged=true;
            for(int a=0;a<ActionCount;++a)for(int s=1;s<3;++s) {
                const auto& actual=next[p].bindings[a][s];const auto& old=previous[p].bindings[a][s];
                unchanged&=actual.source==old.source && actual.index==old.index && actual.sign==old.sign;
            }
            // Migrate a complete untouched preset, never a partial mix that
            // could collide with the user's custom keyboard/mouse bindings.
            if(unchanged)for(int a=0;a<ActionCount;++a)for(int s=1;s<3;++s)next[p].bindings[a][s]=updated[p].bindings[a][s];
        }
    }
    config=next;return true;
}
bool save(std::string& error) {
    error.clear();if(config_path.empty())config_path="saves/controllers.cfg";
    const std::filesystem::path target(config_path),temporary=config_path+".tmp";
    std::error_code ec;if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);
    if(ec){error="Cannot create the controller settings folder.";return false;}
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out<<"GCN_CONTROLLERS 3\n";
    for(int p=0;p<4;++p) {
        const auto& port=configuration()[p];
        out<<"port "<<p<<' '<<(int)port.device<<' '<<port.keyboard<<' '<<port.deadzone<<' '<<port.mouse_sensitivity<<' '
            <<std::quoted(port.controller_id)<<' '<<std::quoted(port.controller_name)<<'\n';
        for(int a=0;a<ActionCount;++a)for(int s=0;s<3;++s) {
            const auto& b=port.bindings[a][s];out<<"bind "<<p<<' '<<a<<' '<<s<<' '<<(int)b.source<<' '<<b.index<<' '<<b.sign<<'\n';
        }
    }
    out.flush();bool ok=(bool)out;out.close();ok=ok && !out.fail();
    if(!ok){error="Cannot write controller settings. Check the folder is writable.";return false;}
#ifdef _WIN32
    ok=MoveFileExW(temporary.wstring().c_str(),target.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,target,ec);ok=!ec;
#endif
    if(!ok){error="Cannot replace controller settings. The previous settings were preserved.";return false;}
    fprintf(stderr,"[input-settings] saved %s\n",config_path.c_str());return true;
}

void event(const SDL_Event& e) {
    const auto now=SDL_GetTicks64();
    if(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) {
        if(e.key.keysym.scancode>0 && e.key.keysym.scancode<SDL_NUM_SCANCODES)keys[e.key.keysym.scancode]=e.type==SDL_KEYDOWN;
    }else if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button<=5)mouse_buttons|=SDL_BUTTON(e.button.button);
    else if(e.type==SDL_MOUSEBUTTONUP && e.button.button<=5)mouse_buttons&=~SDL_BUTTON(e.button.button);
    else if(e.type==SDL_MOUSEMOTION && !ui_block && focused) {
        float sensitivity=.025f;for(const auto& p:configuration())if(p.keyboard && p.device!=Device::Disabled)sensitivity=p.mouse_sensitivity;
        mouse_x=std::clamp(mouse_x+e.motion.xrel*sensitivity,-1.f,1.f);mouse_y=std::clamp(mouse_y+e.motion.yrel*sensitivity,-1.f,1.f);
    }else if(e.type==SDL_MOUSEWHEEL) {wheel=e.wheel.y*(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1);wheel_at=now;}
    else if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)set_focused(false);
    else if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)set_focused(true);
    if(!capturing() || !capture.ready)return;
    const bool gamepad=capture.alternative==0;
    if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode==SDL_SCANCODE_ESCAPE){cancel_capture();return;}
    if(!gamepad) {
        if(e.type==SDL_KEYDOWN && !e.key.repeat)captured({Source::Key,(int)e.key.keysym.scancode,1});
        // Primary mouse buttons operate the binding dialog; choose their binding
        // explicitly there so clicking Cancel can never assign Mouse Left.
        else if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button>=4)captured({Source::MouseButton,e.button.button,1});
        else if(e.type==SDL_MOUSEWHEEL && wheel)captured({Source::MouseWheel,0,wheel>0?1:-1});
    }else if(auto* pad=controller(capture.port)) {
        const auto id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
        if(e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.which==id)captured({Source::Button,e.cbutton.button,1});
        else if(e.type==SDL_CONTROLLERAXISMOTION && e.caxis.which==id && std::abs(e.caxis.value)>20000) {
            if(e.caxis.axis<SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.value>0)captured({Source::Axis,e.caxis.axis,e.caxis.value>0?1:-1});
        }
    }
}
void block(bool value) {
    if(value==ui_block)return;
    ui_block=value;release_guard=true;mouse_x=mouse_y=0;wheel=0;desktop_gamepad_clear_quit();stop_rumble();
    if(!value)cancel_capture();
}
bool blocked(){return ui_block || release_guard;}
void set_focused(bool value){
    if(focused!=value)fprintf(stderr,"[input-focus] host_ms=%llu focused=%d\n",(unsigned long long)SDL_GetTicks64(),value?1:0);
    focused=value;
    if(!value){keys.fill(false);mouse_buttons=0;mouse_x=mouse_y=0;wheel=0;stop_rumble();}
    // Window focus governs keyboard/mouse input. Controllers remain usable in
    // the background; requiring every held button/stick to become neutral here
    // can swallow acceleration and steering until the player releases them.
    // The independent settings-dialog release guard is still enforced.
}
void sample(std::array<PadState,4>& pads,uint64_t now) {
    if(input_generation!=desktop_pointer::generation.load()){input_generation=desktop_pointer::generation;guarded_mouse=mouse_buttons;guarded_keys=keys;mouse_x=mouse_y=0;wheel=0;}
    guarded_mouse&=mouse_buttons;
    for(int k=0;k<SDL_NUM_SCANCODES;++k)guarded_keys[k]=guarded_keys[k] && keys[k];
    if(release_guard && !ui_block && neutral())release_guard=false;
    const float decay=last_sample?std::clamp((now-last_sample)/90.f,0.f,1.f):1.f;
    last_sample=now;if(now>=wheel_at+60)wheel=0;
    pads={};auto& c=configuration();
    for(int p=0;p<4;++p) {
        auto* pad=controller(p);auto& out=pads[p];const auto& port=c[p];
        const bool has_keyboard=port.keyboard || port.device==Device::KeyboardMouse;
        const bool keyboard=has_keyboard && focused;
        out.connected=port.device!=Device::Disabled && (pad || has_keyboard);
        if(!out.connected)continue;
        float values[ActionCount]{};
        const bool menu=desktop_pointer::menu_enabled.load();
        for(int a=0;a<ActionCount;++a)for(const auto& b:port.bindings[a])values[a]=std::max(values[a],value(b,pad,keyboard && !menu,port));
        if(menu && keyboard && !desktop_pointer::text_enabled) {
            const int menu_keys[]={SDL_SCANCODE_RETURN,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_X,SDL_SCANCODE_Y,SDL_SCANCODE_R,SDL_SCANCODE_N,
                SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT};
            for(int a=0;a<10;++a)if(keys[menu_keys[a]] && !guarded_keys[menu_keys[a]])values[a]=1;
            values[StickUp]=values[DUp];values[StickDown]=values[DDown];values[StickLeft]=values[DLeft];values[StickRight]=values[DRight];
        }
        if(!menu && keyboard && desktop_pointer::navigation_enabled && !desktop_pointer::text_enabled) {
            const int arrows[]={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT};
            const Action directions[]={StickUp,StickDown,StickLeft,StickRight};
            for(int i=0;i<4;++i)if(keys[arrows[i]] && !guarded_keys[arrows[i]])values[directions[i]]=1;
            if(keys[SDL_SCANCODE_RETURN] && !guarded_keys[SDL_SCANCODE_RETURN]){values[Start]=0;values[A]=1;}
        }
        // Pointer-enabled menus receive Escape once through their event queue.
        // Sampling it here too can cancel a confirmation opened by that event.
        if(!menu && keyboard && input_default("desktop_escape_back",0)!=0 && !desktop_pointer::text_enabled && keys[SDL_SCANCODE_ESCAPE] && !guarded_keys[SDL_SCANCODE_ESCAPE])values[desktop_pointer::navigation_enabled?B:Start]=1;
        // Start+Select remains reserved for the desktop exit shortcut.
        if(pad && SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK))values[Start]=0;
        for(int a=0;a<12;++a)if(values[a]>.5f)out.buttons|=bits[a];
        out.trig_l=(uint8_t)(values[LAnalog]*255);out.trig_r=(uint8_t)(values[RAnalog]*255);
        auto axis=[&](int negative,int positive){return (uint8_t)std::clamp(128+(int)((values[positive]-values[negative])*100),0,255);};
        out.stick_x=axis(StickLeft,StickRight);out.stick_y=axis(StickDown,StickUp);
        out.cstick_x=axis(CLeft,CRight);out.cstick_y=axis(CDown,CUp);
    }
    last_preview=pads;
    if(blocked())for(auto& pad:pads){const bool connected=pad.connected;pad={};pad.connected=connected;}
    else for(int p=0;p<4;++p)if(auto* pad=controller(p)) {
        const bool motor=pad_motor(p);
        if(motor || rumbling[p])SDL_GameControllerRumble(pad,motor?0x6000:0,motor?0xA000:0,motor?200:0);
        rumbling[p]=motor;
    }
    mouse_x*=1-decay;mouse_y*=1-decay;
}
bool wants_relative_mouse() {
    if(blocked() || !focused || desktop_pointer::menu_enabled)return false;
    for(const auto& p:configuration())if(p.device!=Device::Disabled && (p.keyboard || p.device==Device::KeyboardMouse))
        for(const auto& action:p.bindings)for(const auto& binding:action)if(binding.source==Source::MouseMotion)return true;
    return false;
}
void begin_capture(int p,int a,int slot,uint64_t now){if(valid_port(p) && a>=0 && a<ActionCount && slot>=0 && slot<3)capture={p,a,slot,now,false};}
bool capturing(){return capture.port>=0;}
void cancel_capture(){capture={};}
bool capture_ready(uint64_t now){if(capturing() && !capture.ready && now>=capture.started+250 && neutral())capture.ready=true;return capture.ready;}
void capture_mouse_motion(int axis,int sign){if(capturing() && capture.alternative>0)captured({Source::MouseMotion,axis,sign});}
std::array<PadState,4> preview(){return last_preview;}
void shutdown(){stop_rumble();keys.fill(false);mouse_buttons=0;capture={};}
}
