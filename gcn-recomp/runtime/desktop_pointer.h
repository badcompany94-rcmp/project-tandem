#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <optional>
union SDL_Event;
struct SDL_Window;
namespace desktop_pointer {
struct Region {float x,y,w,h;int action;}; // normalized in the application's menu viewport
struct State {bool menu=false;int hover=-1;float x=0,y=0;};
inline std::atomic<bool> menu_enabled{false};
inline std::atomic<unsigned> generation{0};
inline std::atomic<bool> text_enabled{false};
// Supplemental navigation during a game's results/pause screens. Race bindings
// stay active, while arrows/Enter/Escape can operate the native menu.
inline std::atomic<bool> navigation_enabled{false};
inline std::string typed;
inline std::mutex mutex;
inline std::vector<Region> regions;
inline std::optional<int> clicked;
inline std::string screen;
inline State state;
inline float aspect=4.f/3.f;
inline void context(bool menu){if(menu)navigation_enabled=false;if(menu_enabled.exchange(menu)!=menu){++generation;std::lock_guard lock(mutex);clicked.reset();regions.clear();state.hover=-1;screen.clear();}}
inline void publish(std::string key,std::vector<Region> areas,float viewport_aspect=4.f/3.f){std::lock_guard lock(mutex);if(screen!=key){++generation;clicked.reset();state.hover=-1;screen=std::move(key);}regions=std::move(areas);aspect=viewport_aspect;}
inline State read(){std::lock_guard lock(mutex);auto result=state;result.menu=menu_enabled;return result;}
inline std::optional<int> take(){std::lock_guard lock(mutex);auto result=clicked;clicked.reset();return result;}
inline std::string take_text(){std::lock_guard lock(mutex);auto result=std::move(typed);typed.clear();return result;}
void event(const SDL_Event&,bool blocked);
void script(SDL_Window*,uint32_t frame); // optional SDL-thread automation, GCN_POINTER_SCRIPT
void update(SDL_Window*,bool settings_open);
void shutdown();
}
