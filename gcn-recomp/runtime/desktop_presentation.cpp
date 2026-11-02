#include "desktop_presentation.h"
#include "diagnostic_log.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace desktop_presentation {
namespace {
Settings active;
std::string path;
bool valid(Settings s) {
    return s.internal_scale >= 0 && s.internal_scale <= 4 &&
           (s.refresh_hz == 0 || s.refresh_hz == 120 || s.refresh_hz == 144 ||
            s.refresh_hz == 165 || s.refresh_hz == 240);
}
}
Settings current() { return active; }
int effective_scale(int launch_scale, int menu_scale) {
    return active.internal_scale ? active.internal_scale : std::max(launch_scale,menu_scale);
}
bool preview(Settings next, std::string& error) {
    error.clear();
    if (!valid(next)) { error = "Choose a supported interpolation rate."; return false; }
    // Interpolation and discard must still work on a driver that cannot change an
    // already-disabled interval. A changed driver interval is reapplied as needed.
    if (SDL_GL_GetSwapInterval() != (next.vsync ? 1 : 0) && SDL_GL_SetSwapInterval(next.vsync ? 1 : 0) < 0) {
        error = std::string("Cannot change VSync: ") + SDL_GetError();
        return false;
    }
    active = next;
    diagnostic_log("[presentation-settings] vsync=%d interpolation_hz=%d internal_scale=%d\n", active.vsync, active.refresh_hz,active.internal_scale);
    return true;
}
bool load(const std::string& file, std::string& error) {
    path = file; active = {}; error.clear();
    Settings next;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        if (std::filesystem::file_size(path, ec) > 1024 || ec) {
            error = "Presentation settings are unreadable. Using defaults.";
        } else {
            std::ifstream in(path); std::string tag, extra; int version = 0, vsync = 0;
            bool parsed=bool(in >> tag >> version >> vsync >> next.refresh_hz);
            if(parsed && version==2)parsed=bool(in >> next.internal_scale);
            if (!parsed || (in >> extra) || tag != "GCN_PRESENTATION" ||
                (version != 1 && version != 2) || vsync < 0 || vsync > 1 || !valid(next))
                error = "Invalid presentation settings. Using defaults.";
            else next.vsync = vsync != 0;
        }
    } else if (ec) error = "Cannot read presentation settings. Using defaults.";
    const auto read_error = error;
    if (!read_error.empty()) next = {};
    if (!preview(next, error)) return false;
    error = read_error;
    return error.empty();
}
bool save(std::string& error) {
    error.clear();
    if (path.empty()) { error = "Presentation settings are not initialized."; return false; }
    const std::filesystem::path target(path), temporary = path + ".tmp";
    std::error_code ec;
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) { error = "Cannot create the presentation settings folder."; return false; }
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "GCN_PRESENTATION 2\n" << (active.vsync ? 1 : 0) << ' ' << active.refresh_hz << ' ' << active.internal_scale << '\n';
    out.flush(); bool ok = bool(out); out.close(); ok = ok && !out.fail();
    if (!ok) { error = "Cannot save presentation settings. Check the folder is writable."; return false; }
#ifdef _WIN32
    ok = MoveFileExW(temporary.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, target, ec); ok = !ec;
#endif
    if (!ok) { error = "Cannot replace presentation settings. Previous settings were preserved."; return false; }
    return true;
}
}
