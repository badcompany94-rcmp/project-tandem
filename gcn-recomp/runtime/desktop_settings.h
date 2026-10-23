#pragma once
union SDL_Event;
struct SDL_Window;
bool desktop_settings_init(SDL_Window* window,void* gl_context);
// Returns true for settings-owned input events. Window/device events still pass.
bool desktop_settings_event(const SDL_Event& event);
void desktop_settings_update();
bool desktop_settings_open();
void desktop_settings_show();
void desktop_settings_toggle_fullscreen();
void desktop_settings_draw();
void desktop_settings_shutdown();
