#pragma once
#include "gc_adapter_report.h"
#include <array>
#include <atomic>
#include <string>
#include <SDL.h>

namespace gc_adapter {
// Publish connected adapter ports through SDL, so F1 device assignment, remapping,
// controller menu navigation and existing per-port input all use the same path.
class Pads {
public:
    explicit Pads(int adapter_number);
    ~Pads();
    Pads(const Pads&)=delete;
    Pads& operator=(const Pads&)=delete;
    void update(const Report& report);
    void clear();
    std::array<uint8_t,5> rumble_packet() const;
private:
    struct Slot {
        SDL_Joystick* joy=nullptr;
        SDL_JoystickID id=-1;
        std::string name;
        std::atomic<bool> motor{false},power{false};
    };
    std::array<Slot,4> slots;
    static int SDLCALL rumble(void* userdata,Uint16 low,Uint16 high);
    bool attach(Slot& slot);
    void detach(Slot& slot);
};
}
void desktop_gc_adapter_update();
void desktop_gc_adapter_shutdown();
