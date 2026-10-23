#include "display_config.h"
#include <atomic>

namespace {
std::atomic<bool> enabled{false};
std::atomic<bool> settings_hint{false};
std::atomic<unsigned> ui_render_width{0};
}
void display_set_widescreen(bool value) { enabled.store(value, std::memory_order_relaxed); }
bool display_widescreen() { return enabled.load(std::memory_order_relaxed); }
void display_set_settings_hint(bool value){settings_hint.store(value,std::memory_order_relaxed);}
bool display_settings_hint(){return settings_hint.load(std::memory_order_relaxed);}
void display_set_ui_render_width(unsigned value){ui_render_width.store(value,std::memory_order_relaxed);}
unsigned display_ui_render_width(){return ui_render_width.load(std::memory_order_relaxed);}
