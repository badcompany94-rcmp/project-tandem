// Extend the game's own camera aspect so its projection and object culling agree.
// Menus and movies retain their original presentation; race HUD elements keep their
// proportions when the scanout is widened.
#include "widescreen.h"
#include "race.h"
#include "display_config.h"
#include "gx/render_gl.h"
extern "C" {
#include "recomp_funcs.h"
}
#include <atomic>
#include <algorithm>
extern "C" bool mkdd_online_pause_open();
extern "C" bool mkdd_online_menu_wide();

namespace {
bool hud_started = false;
uint64_t state(const mkdd::Race& race) {
    return (race.exists?1u:0u) | (race.exists && race.screens==1 && race.phase<mkdd::kEnding && race.mode<=mkdd::kEscapeBattle && !race.paused && !mkdd_online_pause_open() && !race.demo?2u:0u) |
        (display_widescreen()?4u:0u) | (mkdd_online_menu_wide()?8u:0u);
}
uint64_t frame_state() {return state(mkdd::race_read());}
void frame(const gx::Batch& batch, const std::vector<uint8_t>&) {
    hud_started = false;
    const bool wide = (batch.user_state&5u)==5u;
    gx::render_set_flat_aspect((wide || (batch.user_state&8u)) ? 16.0f / 9.0f : 4.0f / 3.0f, wide ? 0.75f : 1.0f);
}
float hud_anchor(const gx::Batch& b, const gx::Cmd& c) {
    if (!(b.user_state&2u)) return 0;
    const auto& st = b.states[c.state];
    // Race2D follows the world and its lens flares. Recognise its persistent
    // speedometer, lap label, item frame, or course map before moving any draws.
    // Texture content hashes survive replays and texture allocation changes.
    const uint64_t hash = gx::render_texture_hash(st.tex_id[0]);
    if (hash == 0xbbe54d4067525bc4ULL || hash == 0x78d6316197c6639cULL ||
        hash == 0xbd87ce88ca5b4c3cULL || hash == 0x815368b1461aed9cULL)
        hud_started = true;
    if (!hud_started) return 0;
    float x0 = 1e20f, x1 = -1e20f;
    for (uint32_t i = 0; i < c.count; ++i) {
        const auto& v = b.verts[b.indices[c.first + i]];
        const float x = st.proj[0] * v.pos[0] + st.proj[1];
        x0 = std::min(x0, x); x1 = std::max(x1, x);
    }
    // Keep the entire rank/item/lap group on the left, and the entire timer,
    // map and speedometer group on the right; central announcements stay put.
    // The smaller, second item slot reaches farther toward the centre than
    // the large slot. Both belong to the left half, including their borders
    // and roulette sprites; an outer-third cutoff splits the two slots.
    if (x1 < 0.0f) return -1;
    if (x0 > 0.20f) return 1;
    return 0;
}
const bool installed = [] {
    gx::render_set_frame_hook(frame,true);
    gx::render_set_flat_overlay_hook(hud_anchor,true);
    gx::set_guest_frame_state_hook(frame_state);
    return true;
}();
void widen(CPU* c) {
    if (display_widescreen()) {
        c->f[1].d = (float)(c->f[1].d * (4.0 / 3.0));
        c->ps1[1] = c->f[1].d;
    }
}
}
namespace mkdd {
void widescreen_guest_frame(const Race& race) {
    gx::set_batch_user_state(state(race));
}
}
extern "C" void hle_getPerspAspect__6SystemFv(CPU* c) {
    orig_getPerspAspect__6SystemFv(c);
    widen(c);
}
extern "C" void hle_getPerspAspectDiv2__6SystemFv(CPU* c) {
    orig_getPerspAspectDiv2__6SystemFv(c);
    widen(c);
}
extern "C" void hle_getPerspAspectDiv4__6SystemFv(CPU* c) {
    orig_getPerspAspectDiv4__6SystemFv(c);
    widen(c);
}
