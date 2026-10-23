#pragma once
#include "hw/pad.h"
#include <array>
#include <string>
#include <vector>

union SDL_Event;
struct _SDL_GameController;
namespace desktop_controls {
enum class Source { None, Key, MouseButton, MouseWheel, MouseMotion, Button, Axis };
enum class Device { Disabled, Automatic, Controller, KeyboardMouse };
enum Action { A,B,X,Y,Z,Start,DUp,DDown,DLeft,DRight,LClick,RClick,LAnalog,RAnalog,
              StickLeft,StickRight,StickUp,StickDown,CLeft,CRight,CUp,CDown,ActionCount };
struct Binding {
    Source source=Source::None;
    int index=0,sign=1;
};
struct Port {
    Device device=Device::Disabled;
    std::string controller_id,controller_name;
    bool keyboard=false;
    float deadzone=.12f,mouse_sensitivity=.025f;
    // One gamepad binding and two keyboard/mouse alternatives per action.
    std::array<std::array<Binding,3>,ActionCount> bindings{};
};
struct DeviceInfo { std::string id,name; _SDL_GameController* handle=nullptr; };
using Configuration=std::array<Port,4>;
const char* action_name(int action);
std::string binding_name(const Binding& binding);
Configuration defaults();
Configuration& configuration();
std::vector<DeviceInfo> devices();
_SDL_GameController* controller(int port);
bool assign(int port,Device type,const std::string& id={},const std::string& name={});
// Configuration is loaded/saved on the SDL thread, never part of guest snapshots.
bool load(const char* path,std::string& error);
bool save(std::string& error);
const std::string& path();
void event(const SDL_Event& event);
// Suppress game input while the UI owns it; held controls must be released after.
void block(bool value);
bool blocked();
void set_focused(bool value);
void sample(std::array<PadState,4>& pads,uint64_t now_ms);
void shutdown();
bool wants_relative_mouse();
// Listening for an assignment ignores held controls and the initiating click.
void begin_capture(int port,int action,int alternative,uint64_t now_ms);
bool capturing();
void cancel_capture();
bool capture_ready(uint64_t now_ms);
void capture_mouse_motion(int axis,int sign);
std::array<PadState,4> preview();
}
