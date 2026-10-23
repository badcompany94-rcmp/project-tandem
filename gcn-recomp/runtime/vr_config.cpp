// See vr_config.h and vr_game.h.
#include "vr_config.h"
#include "vr_game.h"
#include "input_log.h"
#include "runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// Messages go to stderr: the headset app pipes it into logcat, and the headless harness
// is run from a shell.
VrConfig vr_config_load(const std::string& dir, const VrConfig& defaults) {
    VrConfig c = defaults;
    const std::string path = dir + "/vr.txt";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "no %s, using VR defaults (units_per_metre %.1f)\n", path.c_str(),
                c.units_per_metre);
        return c;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        double val;
        if (line[0] == '#' || sscanf(line, "%63s %lf", key, &val) != 2) continue;
        if (!strcmp(key, "units_per_metre")) c.units_per_metre = (float)val;
        else if (!strcmp(key, "offset_x")) c.offset_x = (float)val;
        else if (!strcmp(key, "offset_y")) c.offset_y = (float)val;
        else if (!strcmp(key, "offset_z")) c.offset_z = (float)val;
        else if (!strcmp(key, "near_m")) c.near_m = (float)val;
        else if (!strcmp(key, "far_m")) c.far_m = (float)val;
        else if (!strcmp(key, "hud_scale")) c.hud_scale = (float)val;
        else if (!strcmp(key, "hud_distance_m")) c.hud_distance_m = (float)val;
        else if (!strcmp(key, "hud_height_m")) c.hud_height_m = (float)val;
        else if (!strcmp(key, "hud_pitch_deg")) c.hud_pitch_deg = (float)val;
        else if (!strcmp(key, "world_pitch_deg")) c.world_pitch_deg = (float)val;
        else if (!strcmp(key, "background_band")) c.background_band = (float)val;
        else if (!strcmp(key, "foreground_band")) c.foreground_band = (float)val;
        else if (!strcmp(key, "foreground_scale")) c.foreground_scale = (float)val;
        else if (!strcmp(key, "hud_band")) c.hud_band = (float)val;
        else if (!strcmp(key, "hud_band_scale")) c.hud_band_scale = (float)val;
        else if (!strcmp(key, "hud_push_below")) c.hud_push_below = (float)val;
        else if (!strcmp(key, "hud_push_to")) c.hud_push_to = (float)val;
        else if (!strcmp(key, "view_space_world")) c.view_space_world = val != 0;
        else if (!strcmp(key, "theater_scale")) c.theater_scale = (int)val;
        else if (!strcmp(key, "stereo_scale")) c.stereo_scale = (int)val;
        else if (!strcmp(key, "eye_scale")) c.eye_scale = (float)val;
        else if (!strcmp(key, "msaa")) c.msaa = (int)val;
        else if (!strcmp(key, "perf_boost")) c.perf_boost = val != 0;
        else if (!strcmp(key, "log_frames")) c.log_frames = val != 0;
        else if (!strcmp(key, "dump_every")) c.dump_every = (int)val;
        else if (!strcmp(key, "start_in_stereo")) c.start_in_stereo = val != 0;
        else if (!strcmp(key, "first_person")) c.first_person = val != 0;
        else if (!strcmp(key, "transition_s")) c.transition_s = (float)val;
        else if (!strcmp(key, "transition_out_s")) c.transition_out_s = (float)val;
        else if (!strcmp(key, "theater_stereo")) c.theater_stereo = val != 0;
        else if (!strcmp(key, "panel_band")) c.panel_band = (float)val;
        else if (!strcmp(key, "theater_depth")) c.theater_depth = (float)val;
        else c.extra[key] = (float)val;
    }
    fclose(f);
    fprintf(stderr,
            "vr.txt: units_per_metre=%.2f offset=(%.2f,%.2f,%.2f) near=%.3fm far=%.0fm "
            "hud=%.3f@%.1fm height=%.2fm pitch=%.1fdeg world_pitch=%.1fdeg layers=%.4f/%.4fx%.2f hud_band=%.5fx%.2f hud_push=%.1f->%.1f "
            "scale=%d/%d eyes=%.2fx msaa=%d stereo_at_start=%d first_person=%d transition=%.2fs/%.2fs theater_stereo=%d panel_band=%.5f depth=%.2f\n",
            c.units_per_metre, c.offset_x, c.offset_y, c.offset_z, c.near_m, c.far_m,
            c.hud_scale, c.hud_distance_m, c.hud_height_m, c.hud_pitch_deg, c.world_pitch_deg, c.background_band, c.foreground_band, c.foreground_scale,
            c.hud_band, c.hud_band_scale, c.hud_push_below, c.hud_push_to,
            c.theater_scale, c.stereo_scale, c.eye_scale, c.msaa, (int)c.start_in_stereo,
            (int)c.first_person, c.transition_s, c.transition_out_s < 0.0f ? c.transition_s : c.transition_out_s,
            (int)c.theater_stereo, c.panel_band, c.theater_depth);
    for (const auto& [k, v] : c.extra) fprintf(stderr, "vr.txt: %s=%g (for the game)\n", k.c_str(), v);
    return c;
}

namespace vr {
// Function-local, because the hooks are installed from another translation unit's static
// initializer, which may run before this one's.
static GameHooks& hooks() {
    static GameHooks h;
    return h;
}
void set_game_hooks(const GameHooks& h) { hooks() = h; }
const GameHooks& game_hooks() { return hooks(); }

// Written by the frontend's render thread, read by the guest's; a pose is seven floats, so
// a lock is cheaper than anything cleverer.
static std::mutex g_hand_lock;
static HandPose g_hands[2];
static bool g_hand_valid[2];

void set_hand_pose(Hand h, const HandPose* p) {
    std::lock_guard<std::mutex> l(g_hand_lock);
    g_hand_valid[h] = p != nullptr;
    if (p) g_hands[h] = *p;
}

// A read from the guest thread is input (input_log.h, input_pose): logged, and answered
// from the log in a replay. The frontends' own reads are not.
bool hand_pose(Hand h, HandPose* out) {
    bool valid;
    {
        std::lock_guard<std::mutex> l(g_hand_lock);
        valid = g_hand_valid[h];
        if (valid) *out = g_hands[h];
    }
    if (cpu_current()) {
        float v[7];
        memcpy(v, out->pos, sizeof(out->pos));
        memcpy(v + 3, out->rot, sizeof(out->rot));
        input_pose(h == kLeftHand ? "hand0" : "hand1", &valid, v, 7);
        memcpy(out->pos, v, sizeof(out->pos));
        memcpy(out->rot, v + 3, sizeof(out->rot));
    }
    return valid;
}

static EyeView g_eyes[2];
static bool g_eyes_valid;

void set_eye_views(const EyeView* views) {
    std::lock_guard<std::mutex> l(g_hand_lock);
    g_eyes_valid = views != nullptr;
    if (views) g_eyes[0] = views[0], g_eyes[1] = views[1];
}

bool eye_views(EyeView out[2]) {
    bool valid;
    {
        std::lock_guard<std::mutex> l(g_hand_lock);
        valid = g_eyes_valid;
        if (valid) out[0] = g_eyes[0], out[1] = g_eyes[1];
    }
    if (cpu_current()) {
        float v[22];
        for (int e = 0; e < 2; e++) {
            float* d = v + 11 * e;
            memcpy(d, out[e].pos, sizeof(out[e].pos));
            memcpy(d + 3, out[e].rot, sizeof(out[e].rot));
            d[7] = out[e].tan_left; d[8] = out[e].tan_right; d[9] = out[e].tan_up; d[10] = out[e].tan_down;
        }
        input_pose("eyes", &valid, v, 22);
        for (int e = 0; e < 2; e++) {
            const float* d = v + 11 * e;
            memcpy(out[e].pos, d, sizeof(out[e].pos));
            memcpy(out[e].rot, d + 3, sizeof(out[e].rot));
            out[e].tan_left = d[7]; out[e].tan_right = d[8]; out[e].tan_up = d[9]; out[e].tan_down = d[10];
        }
    }
    return valid;
}

VrConfig load_config(const std::string& dir) {
    VrConfig d;
    if (hooks().config_defaults) hooks().config_defaults(d);
    const VrConfig c = vr_config_load(dir, d);
    if (hooks().config_loaded) hooks().config_loaded(c);
    return c;
}
}  // namespace vr
