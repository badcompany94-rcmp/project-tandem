#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct SDL_Window;
namespace desktop_display {
enum class Mode { Windowed, Fullscreen, Borderless };
struct Settings {
    Mode mode=Mode::Windowed;
    int monitor=0,width=1280,height=720;
    bool operator==(const Settings&) const=default;
};
struct Monitor {int index,width,height;std::string name;};
struct Resolution {int width,height;bool operator==(const Resolution&) const=default;};
bool init(SDL_Window*,const std::string& file,std::string& error);
Settings current();
std::vector<Monitor> monitors();
std::vector<Resolution> resolutions(Mode,int monitor);
bool preview(Settings,uint64_t now_ms,std::string& error);
bool keep(std::string& error);
bool revert(std::string& error);
bool tick(uint64_t now_ms,std::string& error);
bool pending();
unsigned seconds_left(uint64_t now_ms);
void shutdown();
}
