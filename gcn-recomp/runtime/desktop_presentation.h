#pragma once
#include <string>

// Desktop render-thread settings. Never read by the guest clock or network code.
namespace desktop_presentation {
struct Settings {
    bool vsync = false;
    int refresh_hz = 0;
    // Zero preserves the launcher's existing scale/menu behavior.
    int internal_scale = 0;
    bool operator==(const Settings&) const = default;
};
Settings current();
bool load(const std::string& file, std::string& error);
bool preview(Settings settings, std::string& error);
bool save(std::string& error);
int effective_scale(int launch_scale, int menu_scale);
}
