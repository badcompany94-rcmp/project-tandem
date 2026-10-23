// Exercise the real SDL mapping and lifecycle with virtual devices, leaving physical
// controllers alone. This needs no game image and no controller button presses.
#define SDL_MAIN_HANDLED
#include "desktop_gamepad.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <SDL.h>

static int checks;
static void require(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        fprintf(stderr, "FAIL: %s (SDL: %s)\n", message, SDL_GetError());
        exit(1);
    }
}

static bool exclude_physical(int i) {
    const char* name = SDL_JoystickNameForIndex(i);
    return !name || strncmp(name, "GCN test ", 9) != 0;
}

static void update() {
    SDL_JoystickUpdate();
    SDL_GameControllerUpdate();
    SDL_Event event;
    while (SDL_PollEvent(&event)) desktop_gamepad_event(event,0);
}

struct VirtualPad {
    SDL_Joystick* joystick = nullptr;
    SDL_JoystickID id = -1;
    explicit VirtualPad(bool is_xbox,const char* profile=nullptr,int product=0,const char* custom_name=nullptr) {
        const char* name = custom_name?custom_name:is_xbox ? "GCN test Xbox" : "GCN test generic";
        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        desc.vendor_id = 0xf00d;
        desc.product_id = product?product:is_xbox ? 2 : 1;
        desc.name = name;
        const int index = SDL_JoystickAttachVirtualEx(&desc);
        require(index >= 0, "attach virtual controller");
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
        std::string mapping = std::string(guid) + "," + name +
            ",a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
            "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
            "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,type:" +
            (profile?profile:is_xbox?"xboxone":"standard")+std::string(",");
        require(SDL_GameControllerAddMapping(mapping.c_str()) >= 0, "register virtual mapping");
        joystick = SDL_JoystickOpen(index);
        require(joystick != nullptr, "open virtual controller");
        id = SDL_JoystickInstanceID(joystick);
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
        update();
    }
    void axis(SDL_GameControllerAxis axis, Sint16 value) {
        require(SDL_JoystickSetVirtualAxis(joystick, axis, value) == 0, "set virtual axis");
    }
    void button(SDL_GameControllerButton button, bool pressed) {
        require(SDL_JoystickSetVirtualButton(joystick, button, pressed) == 0, "set virtual button");
    }
    void detach() {
        if (!joystick) return;
        SDL_JoystickClose(joystick);
        joystick = nullptr;
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_JoystickGetDeviceInstanceID(i) == id) {
                require(SDL_JoystickDetachVirtual(i) == 0, "detach virtual controller");
                break;
            }
        }
        update();
    }
    ~VirtualPad() { detach(); }
};

static PadState read_pad() {
    update();
    PadState p;
    desktop_gamepad_merge(p);
    return p;
}

int main() {
    SDL_SetMainReady();
    desktop_gamepad_configure();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "initialize SDL controllers");
    desktop_gamepad_refresh();
    printf("Detected physical/default controller: %s\n",
           desktop_gamepad_name() ? desktop_gamepad_name() : "none");
    desktop_gamepad_shutdown();

    VirtualPad generic(false);
    desktop_gamepad_refresh(exclude_physical);
    require(desktop_gamepad_name() && !strcmp(desktop_gamepad_name(), "GCN test generic"),
            "use generic controller when no eligible Xbox controller is connected");
    VirtualPad xbox(true);
    desktop_gamepad_shutdown();
    desktop_gamepad_refresh(exclude_physical);
    require(desktop_gamepad_name() && !strcmp(desktop_gamepad_name(), "GCN test Xbox"),
            "prefer Xbox at startup when several controller families are connected");
    auto neutral = read_pad();
    require(neutral.buttons == 0 && neutral.trig_l == 0 && neutral.trig_r == 0,
            "resting controller produces no phantom presses");
    struct ButtonCase { SDL_GameControllerButton button; uint16_t expected; };
    const ButtonCase cases[] = {
        {SDL_CONTROLLER_BUTTON_A, PAD_A}, {SDL_CONTROLLER_BUTTON_B, PAD_B},
        {SDL_CONTROLLER_BUTTON_X, PAD_X}, {SDL_CONTROLLER_BUTTON_Y, PAD_Y},
        {SDL_CONTROLLER_BUTTON_START, PAD_START},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_Z},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT}
    };
    for (const auto& c : cases) {
        xbox.button(c.button, true);
        require(read_pad().buttons == c.expected, "button reaches only its intended control");
        xbox.button(c.button, false);
        require(read_pad().buttons == 0, "released button does not remain held");
    }
    require(desktop_gamepad_keyboard_start(true, false), "independent Enter remains Start");
    desktop_gamepad_keyboard_start(false, false);
    xbox.button(SDL_CONTROLLER_BUTTON_A, true);
    update();
    require(!desktop_gamepad_keyboard_start(true, false),
            "controller A plus synthesized Enter does not become Start");
    xbox.button(SDL_CONTROLLER_BUTTON_A, false);
    update();
    require(!desktop_gamepad_keyboard_start(true, false),
            "a delayed synthesized Enter release does not create a Start press");
    desktop_gamepad_keyboard_start(false, false);
    require(desktop_gamepad_keyboard_start(true, false),
            "a later independent keyboard Start still works");
    desktop_gamepad_keyboard_start(false, false);
    require(desktop_gamepad_keyboard_start(false, true), "Tab fallback produces Start");

    xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, 32767);
    xbox.axis(SDL_CONTROLLER_AXIS_LEFTY, -32768);
    xbox.axis(SDL_CONTROLLER_AXIS_RIGHTX, -32768);
    xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 32767);
    auto sticks = read_pad();
    require(sticks.stick_x > 200 && sticks.stick_y > 200 &&
            sticks.cstick_x < 50 && sticks.cstick_y < 50,
            "both sticks have correct direction and useful travel");
    for (int i = SDL_CONTROLLER_AXIS_LEFTX; i <= SDL_CONTROLLER_AXIS_RIGHTY; ++i)
        xbox.axis((SDL_GameControllerAxis)i, 0);
    update();
    PadState keyboard;
    keyboard.buttons = PAD_A;
    keyboard.stick_x = 220;
    desktop_gamepad_merge(keyboard);
    require(keyboard.buttons == PAD_A && keyboard.stick_x == 220,
            "centered gamepad preserves simultaneous keyboard input");

    xbox.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -20000);
    auto soft = read_pad();
    require(soft.trig_l > 0 && !(soft.buttons & PAD_L), "soft trigger pull stays analog");
    xbox.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 10000);
    xbox.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 10000);
    auto drift = read_pad();
    require((drift.buttons & (PAD_L | PAD_R)) == (PAD_L | PAD_R) &&
            drift.trig_l > 128 && drift.trig_r > 128, "triggers engage before the hard stop");

    xbox.button(SDL_CONTROLLER_BUTTON_A, true);
    require((read_pad().buttons & PAD_A) != 0, "accelerator held before disconnect");
    xbox.detach();
    desktop_gamepad_refresh(exclude_physical);
    require(desktop_gamepad_name() && !strcmp(desktop_gamepad_name(), "GCN test generic"),
            "fall back to the remaining controller after disconnect");
    require(read_pad().buttons == 0, "disconnect releases the accelerator and triggers");
    {
        VirtualPad reconnected(true);
        desktop_gamepad_refresh(exclude_physical);
        reconnected.button(SDL_CONTROLLER_BUTTON_A,true);update();
        require(desktop_gamepad_name() && !strcmp(desktop_gamepad_name(), "GCN test Xbox"),
                "using a reconnected Xbox controller selects it automatically");
        require(read_pad().buttons == PAD_A, "reconnected controller can accelerate");
    }
    desktop_gamepad_refresh(exclude_physical);
    struct Profile { const char* type;SDL_GameControllerType expected; };
    const Profile profiles[]={{"ps4",SDL_CONTROLLER_TYPE_PS4},{"ps5",SDL_CONTROLLER_TYPE_PS5},
        {"switchpro",SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO},{"joyconpair",SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR}};
    int product=20;
    for(const auto& profile:profiles) {
        VirtualPad pad(false,profile.type,product++,"GCN test family");
        desktop_gamepad_refresh(exclude_physical);
        pad.button(SDL_CONTROLLER_BUTTON_A,true);update();
        require(desktop_gamepad_name() && !strcmp(desktop_gamepad_name(),"GCN test family"),"activity selects Switch/PlayStation controller");
        require(SDL_GameControllerGetType(SDL_GameControllerFromInstanceID(pad.id))==profile.expected,"SDL identifies the intended controller family");
        pad.button(SDL_CONTROLLER_BUTTON_A,false);update();
        for(const auto& c:cases) {
            pad.button(c.button,true);require(read_pad().buttons==c.expected,"family button mapping reaches only intended control");
            pad.button(c.button,false);require(read_pad().buttons==0,"family button release is clean");
        }
        pad.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);pad.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);
        auto drift=read_pad();require(drift.trig_l==255 && drift.trig_r==255 && (drift.buttons&(PAD_L|PAD_R))==(PAD_L|PAD_R),"digital Switch ZL/ZR and PS trigger endpoints engage drift");
        pad.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);pad.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);update();
        SDL_Event escape{};escape.type=SDL_KEYDOWN;escape.key.keysym.scancode=SDL_SCANCODE_ESCAPE;
        desktop_gamepad_event(escape,1000);
        require(!desktop_gamepad_quit_requested(1000,true),"Escape waits for controller-profile state");
        require(desktop_gamepad_quit_requested(1150,true),"standalone Escape quits after a brief debounce");
        require(!desktop_gamepad_quit_requested(1200,true),"Escape is consumed once");
        pad.button(SDL_CONTROLLER_BUTTON_START,true);update();
        desktop_gamepad_event(escape,2000);
        require(!desktop_gamepad_quit_requested(2150,true),"Menu translated to Escape still pauses without quitting");
        require(read_pad().buttons==PAD_START,"Menu alone remains the game Start button");
        require(!desktop_gamepad_quit_requested(20000,true),"holding Menu alone never quits");
        pad.button(SDL_CONTROLLER_BUTTON_BACK,true);update();
        require(!desktop_gamepad_quit_requested(30000,true),"quit chord starts a fresh hold");
        require(read_pad().buttons==0,"quit chord does not inject repeated Start presses");
        require(!desktop_gamepad_quit_requested(31999,true),"short hold does not quit");
        require(desktop_gamepad_quit_requested(32000,true),"two-second Menu/Select hold quits");
        require(!desktop_gamepad_quit_requested(33000,false),"unfocused game cannot quit through the controller");
        require(!desktop_gamepad_quit_requested(34000,true),"focus return restarts the hold");
        pad.button(SDL_CONTROLLER_BUTTON_BACK,false);update();
        require(!desktop_gamepad_quit_requested(35500,true),"releasing either button cancels the hold");
        pad.button(SDL_CONTROLLER_BUTTON_BACK,true);update();
        require(!desktop_gamepad_quit_requested(36000,true),"repressing starts a new hold");
        pad.detach();desktop_gamepad_refresh(exclude_physical);
        require(!desktop_gamepad_quit_requested(39000,true),"disconnect cancels quit hold without stuck buttons");
    }
    desktop_gamepad_shutdown();
    generic.detach();
    SDL_Quit();
    printf("PASS: %d controller integration checks\n", checks);
    return 0;
}
