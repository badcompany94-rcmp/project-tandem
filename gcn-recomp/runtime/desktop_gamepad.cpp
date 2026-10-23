#include "desktop_gamepad.h"
#include "input_defaults.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <SDL.h>

namespace {
SDL_GameController* controller;
std::vector<SDL_GameController*> available;
bool rumbling;
bool suppress_enter;
bool quit_key_pending,quit_holding;
uint64_t quit_key_at,quit_hold_at;

bool xbox(SDL_GameControllerType type) {
    return type == SDL_CONTROLLER_TYPE_XBOX360 || type == SDL_CONTROLLER_TYPE_XBOXONE;
}

uint8_t axis_to_u8(int value, bool invert) {
    float f = value / 32767.0f;
    if (invert) f = -f;
    return (uint8_t)std::clamp(128 + (int)(f * 100.0f), 0, 255);
}
void select(SDL_GameController* next) {
    if(next==controller)return;
    if(controller && rumbling)SDL_GameControllerRumble(controller,0,0,0);
    controller=next;rumbling=false;suppress_enter=false;quit_holding=false;
    if(controller)fprintf(stderr,"[input] gamepad: %s (default mapping)\n",SDL_GameControllerName(controller));
}
void activity(SDL_JoystickID instance) {
    for(auto* pad:available)if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==instance){select(pad);break;}
}
}  // namespace

void desktop_gamepad_configure() {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4,"1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5,"1");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,"1");
}

void desktop_gamepad_shutdown() {
    select(nullptr);
    for(auto* pad:available)SDL_GameControllerClose(pad);
    available.clear();quit_key_pending=false;quit_holding=false;
}

const char* desktop_gamepad_name() {
    return controller ? SDL_GameControllerName(controller) : nullptr;
}

int desktop_gamepad_count(){return (int)available.size();}
SDL_GameController* desktop_gamepad_at(int i){return i>=0 && i<(int)available.size()?available[i]:nullptr;}
SDL_GameController* desktop_gamepad_active(){return controller;}
void desktop_gamepad_clear_quit(){quit_key_pending=false;quit_holding=false;}

void desktop_gamepad_refresh(bool (*excluded)(int)) {
    std::vector<SDL_JoystickID> eligible;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i) || (excluded && excluded(i))) continue;
        eligible.push_back(SDL_JoystickGetDeviceInstanceID(i));
    }
    for(auto it=available.begin();it!=available.end();) {
        const auto id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it));
        if(!SDL_GameControllerGetAttached(*it) || std::find(eligible.begin(),eligible.end(),id)==eligible.end()) {
            if(*it==controller){fprintf(stderr,"[input] gamepad disconnected\n");select(nullptr);}
            SDL_GameControllerClose(*it);it=available.erase(it);
        }else ++it;
    }
    for(int i=0;i<SDL_NumJoysticks();++i) {
        const auto id=SDL_JoystickGetDeviceInstanceID(i);
        if(std::find(eligible.begin(),eligible.end(),id)==eligible.end())continue;
        bool opened=false;for(auto* pad:available)if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==id)opened=true;
        if(!opened){if(auto* pad=SDL_GameControllerOpen(i))available.push_back(pad);}
    }
    if(!controller && !available.empty()) {
        auto* first=available.front();
        for(auto* pad:available)if(xbox(SDL_GameControllerGetType(pad))){first=pad;break;}
        select(first);
    }
}

void desktop_gamepad_event(const SDL_Event& event,uint64_t now_ms) {
    if(event.type==SDL_CONTROLLERBUTTONDOWN)activity(event.cbutton.which);
    else if(event.type==SDL_CONTROLLERAXISMOTION) {
        const bool stick=event.caxis.axis<=SDL_CONTROLLER_AXIS_RIGHTY;
        if(stick?abs(event.caxis.value)>16000:event.caxis.value>16000)activity(event.caxis.which);
    }else if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_ESCAPE && input_default("desktop_escape_back",0)==0) {
        quit_key_pending=true;quit_key_at=now_ms;
    }
}

bool desktop_gamepad_quit_requested(uint64_t now_ms,bool focused) {
    if(!focused){quit_key_pending=false;quit_holding=false;return false;}
    // Desktop controller profiles sometimes emit Escape for Menu/Select. Give
    // the physical state a short time to arrive, so ordinary pause cannot quit.
    for(auto* pad:available)if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START) ||
                              SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK))quit_key_pending=false;
    const bool held=controller && SDL_GameControllerGetAttached(controller) &&
        SDL_GameControllerGetButton(controller,SDL_CONTROLLER_BUTTON_START) &&
        SDL_GameControllerGetButton(controller,SDL_CONTROLLER_BUTTON_BACK);
    if(!held)quit_holding=false;
    else if(!quit_holding){quit_holding=true;quit_hold_at=now_ms;}
    else if(now_ms-quit_hold_at>=2000)return true;
    if(quit_key_pending && now_ms-quit_key_at>=150){quit_key_pending=false;return true;}
    return false;
}

bool desktop_gamepad_keyboard_start(bool enter, bool tab) {
    if (!enter) suppress_enter = false;
    else if (controller && SDL_GameControllerGetAttached(controller) &&
             SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) suppress_enter = true;
    return tab || (enter && !suppress_enter);
}

void desktop_gamepad_merge(PadState& p) {
    if (!controller || !SDL_GameControllerGetAttached(controller)) return;
    auto b = [](SDL_GameControllerButton button) {
        return SDL_GameControllerGetButton(controller, button) != 0;
    };
    if (b(SDL_CONTROLLER_BUTTON_A)) p.buttons |= PAD_A;
    if (b(SDL_CONTROLLER_BUTTON_B)) p.buttons |= PAD_B;
    if (b(SDL_CONTROLLER_BUTTON_X)) p.buttons |= PAD_X;
    if (b(SDL_CONTROLLER_BUTTON_Y)) p.buttons |= PAD_Y;
    if (b(SDL_CONTROLLER_BUTTON_START) && !b(SDL_CONTROLLER_BUTTON_BACK)) p.buttons |= PAD_START;
    if (b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) p.buttons |= PAD_Z;
    if (b(SDL_CONTROLLER_BUTTON_DPAD_UP)) p.buttons |= PAD_UP;
    if (b(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) p.buttons |= PAD_DOWN;
    if (b(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) p.buttons |= PAD_LEFT;
    if (b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p.buttons |= PAD_RIGHT;
    const int lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const int ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    if (abs(lx) > 4000 || abs(ly) > 4000) {
        p.stick_x = axis_to_u8(lx, false);
        p.stick_y = axis_to_u8(ly, true);
    }
    const int rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    const int ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    if (abs(rx) > 1000 || abs(ry) > 1000) {
        p.cstick_x = axis_to_u8(rx, false);
        p.cstick_y = axis_to_u8(ry, true);
    }
    const int lt = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const int rt = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (lt > 1000) {
        p.trig_l = std::max(p.trig_l, (uint8_t)(lt * 255 / 32767));
        if (lt > 16384) p.buttons |= PAD_L;
    }
    if (rt > 1000) {
        p.trig_r = std::max(p.trig_r, (uint8_t)(rt * 255 / 32767));
        if (rt > 16384) p.buttons |= PAD_R;
    }
}

void desktop_gamepad_rumble(bool motor) {
    if (controller && (motor || rumbling))
        SDL_GameControllerRumble(controller, motor ? 0x6000 : 0, motor ? 0xA000 : 0, motor ? 200 : 0);
    rumbling = motor;
}
