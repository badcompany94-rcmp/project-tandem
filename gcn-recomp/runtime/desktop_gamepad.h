#pragma once
#include "hw/pad.h"

union SDL_Event;
struct _SDL_GameController;
// Borrowed SDL handles, valid until refresh/shutdown. The settings frontend can
// route these devices to individual ports without changing automatic selection.
int desktop_gamepad_count();
_SDL_GameController* desktop_gamepad_at(int index);
_SDL_GameController* desktop_gamepad_active();
void desktop_gamepad_clear_quit();
// Enable native Switch/PS4/PS5 drivers before SDL initialization. Switch face
// buttons follow their printed labels; PlayStation uses Cross/Circle/Square/Triangle.
void desktop_gamepad_configure();
// Xbox is preferred initially. Meaningful activity can select another attached
// pad. Devices claimed by a custom control file are left to that file instead.
void desktop_gamepad_refresh(bool (*excluded)(int) = nullptr);
void desktop_gamepad_event(const SDL_Event&, uint64_t now_ms);
// Escape requests quit after a brief controller-profile suppression interval.
// Holding the active controller's Start+Back for two seconds also requests quit.
bool desktop_gamepad_quit_requested(uint64_t now_ms, bool focused);
void desktop_gamepad_merge(PadState& pad);
// Desktop controller profiles may also emit Enter for A. Suppress that duplicate
// until Enter is released, while preserving independent keyboard Start input.
bool desktop_gamepad_keyboard_start(bool enter, bool tab);
void desktop_gamepad_rumble(bool motor);
void desktop_gamepad_shutdown();
const char* desktop_gamepad_name();
