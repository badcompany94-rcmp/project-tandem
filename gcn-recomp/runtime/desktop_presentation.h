#pragma once
#include <string>

// Desktop render-thread settings. Never read by the guest clock or network code.
namespace desktop_presentation {
struct Settings {
    bool vsync = false;
    int refresh_hz = 0;
    bool operator==(const Settings&) const = default;
};
Settings current();
bool load(const std::string& file, std::string& error);
bool preview(Settings settings, std::string& error);
bool save(std::string& error);
}
