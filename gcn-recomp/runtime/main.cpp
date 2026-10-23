#include "diagnostic_log.h"
// Entry point and SDL frontend: window, input, audio. The main thread executes GX
// batches produced by the guest thread.
#include "runtime.h"
#include "platform.h"
#include "input_script.h"
#include "input_log.h"
#include "input_map.h"
#include "desktop_gamepad.h"
#include "desktop_controls.h"
#include "desktop_pointer.h"
#include "desktop_settings.h"
#include "desktop_stats.h"
#include "presentation_stats.h"
#include "display_config.h"
#include "gx/render_gl.h"
#include "gx/gl.h"
#include "hw/pad.h"
#include "vr_game.h"
#include "telemetry.h"
#include "record.h"
#ifdef GCN_OPENXR
#include "xr_desktop.h"
#endif
#ifdef _WIN32
// Console app: keep our own main() rather than SDL2main's WinMain shim.
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

uint32_t boot_load(const char* iso_path);
void debug_dump_threads();
void debug_sampler_start();
bool write_png(const char* path, const uint8_t* rgba, int w, int h);

// ---------------------------------------------------------------------------
// --eye renders through the stereo path into an offscreen target and shows that in the
// window instead of the flat frame: the eye sits where the game's camera is, looking
// straight ahead with a 90 degree field, so a VR change can be looked at here before it
// goes anywhere near a headset. --first-person puts the eye wherever the game project's
// eye hook says (render_set_first_person), on the player's vehicle for a racing game.
// --dump-dir/--dump-every write the eye's frames as PNGs.
// ---------------------------------------------------------------------------
static bool g_eye_mode = false;
static GLuint g_eye_fbo, g_eye_tex, g_eye_depth;
static const int kEyeW = 960, kEyeH = 720;
// The game's VR settings (eye_init), for the eye's planes and the HUD frame's placement.
static VrConfig g_eye_cfg;

static void eye_init() {
    glGenTextures(1, &g_eye_tex);
    glBindTexture(GL_TEXTURE_2D, g_eye_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kEyeW, kEyeH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &g_eye_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_eye_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kEyeW, kEyeH);
    glGenFramebuffers(1, &g_eye_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_eye_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_eye_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_eye_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fatal("eye fbo incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // The game's VR settings as a headset would have them -- its defaults, then ./vr.txt --
    // so the preview shows what the game's hooks do: its world pitch, its depth layers and
    // what its eye filter leaves out.
    const VrConfig& c = g_eye_cfg = vr::load_config(".");
    gx::render_set_world_pitch(c.world_pitch_deg * 3.14159265f / 180.0f);
    gx::render_set_depth_layers(c.background_band, c.foreground_band, c.foreground_scale, c.hud_band, c.hud_band_scale);
    gx::render_set_hud_push(c.hud_push_below, c.hud_push_to);
    gx::render_set_view_space_world(c.view_space_world);
    gx::render_set_eye_filter(vr::game_hooks().eye_filter);
    gx::render_set_vr_morph(1.0f, nullptr);
}

// Column-major, as glUniformMatrix4fv takes them. The view is the identity: the eye is
// wherever the renderer's world transform puts it.
static void eye_matrices(float* proj, float* view, float* hud) {
    const float fov = 1.0f;   // tan(45 deg): a 90 degree vertical field
    const float aspect = (float)kEyeW / (float)kEyeH;
    const float u = g_eye_cfg.units_per_metre;
    const float n = g_eye_cfg.near_m * u, f = g_eye_cfg.far_m * u;
    memset(proj, 0, 16 * sizeof(float));
    proj[0] = 1.0f / (fov * aspect);
    proj[5] = 1.0f / fov;
    proj[10] = -(f + n) / (f - n);
    proj[11] = -1.0f;
    proj[14] = -(2.0f * f * n) / (f - n);
    memset(view, 0, 16 * sizeof(float));
    view[0] = view[5] = view[10] = view[15] = 1.0f;
    gx::render_hud_frame(g_eye_cfg.hud_distance_m * u, fov, g_eye_cfg.hud_scale, g_eye_cfg.hud_height_m * u,
                         g_eye_cfg.hud_pitch_deg * 3.14159265f / 180.0f, hud);
}

static void eye_dump(uint32_t n) {
    std::vector<uint8_t> px((size_t)kEyeW * kEyeH * 4), fl(px.size());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_eye_fbo);
    glReadPixels(0, 0, kEyeW, kEyeH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const size_t stride = (size_t)kEyeW * 4;
    for (int y = 0; y < kEyeH; y++) memcpy(&fl[y * stride], &px[(size_t)(kEyeH - 1 - y) * stride], stride);
    for (size_t i = 3; i < fl.size(); i += 4) fl[i] = 255;
    char path[512];
    snprintf(path, sizeof(path), "%s/eye_%05u.png", gx::g_dump_dir, n);
    write_png(path, fl.data(), kEyeW, kEyeH);
}

// Stringify, to quote the required GL version in a message without a format argument.
#define GCN_STR_(x) #x
#define GCN_STR(x) GCN_STR_(x)

static void on_interrupt() {
    // GCN_RAM_DUMP=<path>: the guest RAM at the interrupt, for reading the game's state offline.
    if (const char* path = getenv("GCN_RAM_DUMP")) {
        FILE* f = fopen(path, "wb");
        if (!f) perror("GCN_RAM_DUMP");
        else { fwrite(g_mem, 1, RAM_SIZE, f); fclose(f); diagnostic_log( "RAM written to %s\n", path); }
    }
    plat_watchdog(2, 2);  // never hang in here (stdio locks may be held by other threads)
    input_log_end();
    input_map_shutdown();  // a wheel left pushing is a wheel pushing at nobody
    debug_dump_threads();
    plat_exit_now(1);
}

static void on_fault(const void* fault_addr, int code) {
    if (const char* path = getenv("GCN_RAM_DUMP")) {
        if (FILE* f = fopen(path, "wb")) { fwrite(g_mem, 1, RAM_SIZE, f); fclose(f); diagnostic_log( "RAM written to %s\n", path); }
    }
    uintptr_t a = (uintptr_t)fault_addr;
    if (a >= (uintptr_t)g_mem && a < (uintptr_t)g_mem + 0x40000000)
        diagnostic_log( "\nFAULT: guest memory access at (addr & 0x3FFFFFFF) = %08lX\n", (unsigned long)(a - (uintptr_t)g_mem));
    else
        diagnostic_log( "\nFAULT: host address %p (code %d)\n", fault_addr, code);
    plat_watchdog(2, 2);
    input_log_end();
    input_map_shutdown();
    plat_backtrace_print();
    debug_dump_threads();
    plat_exit_now(1);
}

bool audio_open();

// ---------------------------------------------------------------------------
// Configurable desktop inputs -> all four console ports.
// ---------------------------------------------------------------------------
// The keyboard, the first game controller and controls.txt, merged into `p`: buttons are
// added, and a stick or trigger is taken over only while it is pushed, so that they can
// share the pad with a headset's controllers (xr_desktop.cpp).
static void merge_desktop_pad(PadState& p) {
    std::array<PadState,4> pads;desktop_controls::sample(pads,SDL_GetTicks64());
    const auto& local=pads[0];
    p.connected|=local.connected;p.buttons|=local.buttons;
    if(local.stick_x!=128 || local.stick_y!=128){p.stick_x=local.stick_x;p.stick_y=local.stick_y;}
    if(local.cstick_x!=128 || local.cstick_y!=128){p.cstick_x=local.cstick_x;p.cstick_y=local.cstick_y;}
    p.trig_l=std::max(p.trig_l,local.trig_l);p.trig_r=std::max(p.trig_r,local.trig_r);
    if(!desktop_controls::blocked())input_map_apply(p,!desktop_pointer::menu_enabled);
}

static void update_pad() {
    std::array<PadState,4> pads;desktop_controls::sample(pads,SDL_GetTicks64());
    if(!desktop_controls::blocked()) {
        if(pads[0].connected)input_map_apply(pads[0],!desktop_pointer::menu_enabled);
        for(int port=0;port<4;++port)input_script_apply(pads[port],port);
    }
    for(int port=0;port<4;++port)pad_set_state(port,pads[port]);
}

// The window's events. False once the user has asked to quit.
static bool pump_events(SDL_Window* window) {
    desktop_pointer::script(window,gx::g_frames_submitted.load());
    SDL_Event e;
    bool running = true;
    while (SDL_PollEvent(&e)) {
        const bool settings_owned=desktop_settings_event(e);
        desktop_pointer::event(e,settings_owned || desktop_controls::blocked());
        desktop_controls::event(e);
        if(!settings_owned || e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERAXISMOTION)
            desktop_gamepad_event(e,SDL_GetTicks64());
        input_map_event(e);
        if(settings_owned)continue;
        if (e.type == SDL_QUIT) {
            diagnostic_log( "[exit] window-close event\n");
            running = false;
        }
        else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED ||
                 e.type == SDL_CONTROLLERDEVICEREMAPPED)
            desktop_gamepad_refresh(input_map_binds_index);
        else if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F10) desktop_stats_toggle();
        else if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11) {
            desktop_settings_toggle_fullscreen();
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                 (e.cbutton.button == SDL_CONTROLLER_BUTTON_START || e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK))
            diagnostic_log( "[input] controller %s pressed\n",
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_START ? "Menu" : "Select");
        else if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                 (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE || e.key.keysym.scancode == SDL_SCANCODE_TAB ||
                  e.key.keysym.scancode == SDL_SCANCODE_RETURN))
            diagnostic_log( "[input] keyboard %s received\n", SDL_GetScancodeName(e.key.keysym.scancode));
    }
    desktop_settings_update();
    if(!desktop_settings_open() && desktop_gamepad_quit_requested(SDL_GetTicks64(),SDL_GetKeyboardFocus()!=nullptr)) {
        diagnostic_log("[exit] desktop shortcut\n");running=false;
    }
    return running;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    plat_game_scheduling();
    // The game resetting the console starts it again in a fresh process, as a power cycle
    // would -- except in a replay, which would only replay itself from the start again.
    static char** s_argv = argv;
    set_reset_handler([](uint32_t, uint32_t) {
        if (input_replay_active()) return;
        plat_relaunch(s_argv);
    });
    // Default: the image the build was configured with, else the first image in ./rom
    std::string iso_default = GCN_DEFAULT_ISO;
    if (!plat_readable(iso_default.c_str())) iso_default = plat_find_file("rom", ".iso");
    if (iso_default.empty()) iso_default = plat_find_file("rom", ".ciso");
    const char* iso = iso_default.c_str();
    bool headless = false, hidden = false, input_log = true, first_person = false, fast = false, vr = false;
    uint32_t frame_limit = 0;
    bool fullscreen = false, dump_output = false;
    int window_w = 1280, window_h = 960;
    std::string input_log_dir, replay_dir;
    // --record=<file>[ --record-size=WxH]: a movie of the window with the game's audio (record.h).
    const char* record_to = nullptr;
    int record_w = 2560, record_h = 1440;
    const char* controls = "controls.txt";
    bool test_ffb = false;
    // --telemetry[=host:port]: the game's motion to a motion platform (telemetry.h).
    const char* telemetry_to = getenv("GCN_TELEMETRY");
    int scale = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--log-all")) for (auto& e : g_log_enabled) e = true;
        else if (!strncmp(argv[i], "--log=", 6)) {
            if (!log_enable_list(argv[i] + 6)) { diagnostic_log( "--log: unknown category in %s (cpu,os,hw,dvd,gx,vi,si,exi,dsp,ai,thr)\n", argv[i] + 6); return 1; }
        }
        else if (!strcmp(argv[i], "--eye")) g_eye_mode = true;
        else if (!strcmp(argv[i], "--vr")) vr = true;
        else if (!strcmp(argv[i], "--widescreen")) display_set_widescreen(true);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = true;
        else if (!strcmp(argv[i], "--dump-output")) dump_output = true;
        else if (!strncmp(argv[i], "--resolution=", 13)) {
            if (sscanf(argv[i] + 13, "%dx%d", &window_w, &window_h) != 2 ||
                window_w < 320 || window_h < 240 || window_w > 7680 || window_h > 4320) {
                diagnostic_log( "Invalid --resolution; expected WIDTHxHEIGHT\n");
                return 1;
            }
        }
        else if (!strcmp(argv[i], "--list-controls")) return input_map_probe();
        else if (!strcmp(argv[i], "--test-ffb")) test_ffb = true;
        else if (!strncmp(argv[i], "--controls=", 11)) controls = argv[i] + 11;
        else if (!strcmp(argv[i], "--telemetry")) telemetry_to = "";
        else if (!strncmp(argv[i], "--telemetry=", 12)) telemetry_to = argv[i] + 12;
        else if (!strcmp(argv[i], "--first-person")) first_person = true;
        else if (!strcmp(argv[i], "--no-input-log")) input_log = false;
        else if (!strncmp(argv[i], "--input-log=", 12)) input_log_dir = argv[i] + 12;
        else if (!strncmp(argv[i], "--replay=", 9)) replay_dir = argv[i] + 9;
        else if (!strcmp(argv[i], "--sample")) debug_sampler_start();
        else if (!strcmp(argv[i], "--no-music")) { void audio_mute_music(bool); audio_mute_music(true); }
        else if (!strncmp(argv[i], "--record=", 9)) record_to = argv[i] + 9;
        else if (!strncmp(argv[i], "--record-size=", 14)) sscanf(argv[i] + 14, "%dx%d", &record_w, &record_h);
        else if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--hidden")) hidden = true;
        else if (!strcmp(argv[i], "--no-stats")) desktop_stats_enable(false);
        else if (!strcmp(argv[i], "--fast")) fast = true;
        else if (!strncmp(argv[i], "--frames=", 9)) frame_limit = (uint32_t)strtoul(argv[i] + 9, nullptr, 10);
        else if (!strcmp(argv[i], "--cull-swap")) gx::g_cull_swap = true;
        else if (!strncmp(argv[i], "--scale=", 8)) scale = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--dump-dir=", 11)) gx::g_dump_dir = argv[i] + 11;
        else if (!strncmp(argv[i], "--dump-every=", 13)) gx::g_dump_every = atoi(argv[i] + 13);
        else if (argv[i][0] != '-') iso = argv[i];
    }
    if (test_ffb) return input_map_test_ffb(controls);
    plat_install_crash_handlers(on_interrupt, on_fault);
    if (record_to && !record::start(record_to, record_w, record_h, 60)) return 1;
    if (telemetry_to && !telemetry::configure(telemetry_to)) {
        diagnostic_log( "--telemetry: could not set up the telemetry socket\n");
        return 1;
    }
#ifndef GCN_OPENXR
    if (vr) {
        diagnostic_log( "--vr: this build has no OpenXR frontend (GCN_OPENXR is off, or this platform has none).\n");
        return 1;
    }
#endif

    if (!plat_readable(iso)) {
        diagnostic_log( "No game image found. Put your " GCN_GAME_TITLE " .iso or .ciso in rom/ or pass its path.\n");
        return 1;
    }
    mem_init();
    timing_init();
    if (fast) clock_set_scale(0);  // run as fast as the host allows; the game sees 60 Hz regardless
    input_script_init();

    // Every run records what the guest read from the controller, so that a route to
    // wherever something went wrong can be played back with --replay=<dir>. See
    // input_log.h. --no-input-log turns it off; --input-log=<dir> names the directory.
    // The memory card is snapshotted into the log, and a replay runs on a scratch copy
    // of that snapshot rather than the real card.
    std::string replay_card;
    if (!replay_dir.empty()) {
        if (!input_replay_load(replay_dir, replay_card)) return 1;
        if (!replay_card.empty()) g_memcard_path = replay_card.c_str();
    }
    configure_game_boot();
    if (input_log) {
        if (input_log_dir.empty()) {
            char stamp[64];
            time_t now = time(nullptr);
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
            input_log_dir = std::string("saves/inputs/") + stamp;
        }
        input_log_start(input_log_dir, replay_card.empty() ? "saves/memcard_a.raw" : replay_card);
    }

    if (headless) {
        uint32_t entry = boot_load(iso);
        threads_start_boot(entry);
        for (;;) {
            PadState p;
            p.connected = true;
            input_script_apply(p);
            pad_set_state(0, p);
            auto b = gx::take_batch(5);  // discard
            (void)b;
        }
    }

#ifdef _WIN32
    SDL_SetMainReady();
#endif
    // Joysticks are read without the window's focus: in --vr the headset has it, and a
    // wheel or pedals have no reason to stop when another window is clicked.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    desktop_gamepad_configure();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) fatal("SDL_Init: %s", SDL_GetError());
    input_map_load(controls);
    desktop_gamepad_refresh(input_map_binds_index);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    const bool gl_debug = getenv("GCN_GL_DEBUG") != nullptr;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG |
                                                  (gl_debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0));
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* win = SDL_CreateWindow(GCN_APP_LABEL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       window_w, window_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                       (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) |
                                       (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!win) fatal("SDL_CreateWindow: %s", SDL_GetError());
    // Ask for the newest core profile first: the renderer needs only GCN_GL_MAJOR.MINOR, but an
    // OpenXR runtime may want more of the context it shares (Meta's wants 4.0), and drivers
    // hand back exactly the version asked for.
    SDL_GLContext ctx = nullptr;
    static const int kTry[][2] = {{4, 6}, {4, 5}, {4, 3}, {4, 1}, {4, 0}};
    for (const auto& v : kTry) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v[0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v[1]);
        if ((ctx = SDL_GL_CreateContext(win))) break;
    }
    if (!ctx) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GCN_GL_MAJOR);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GCN_GL_MINOR);
        ctx = SDL_GL_CreateContext(win);
    }
    static const char* kGLHelp =
        "  The renderer needs an OpenGL " GCN_STR(GCN_GL_MAJOR) "." GCN_STR(GCN_GL_MINOR)
        " core profile. If this host only\n"
        "  offers legacy OpenGL (\"GDI Generic\" 1.1, typical for a VM with no GL driver),\n"
        "  install a GL implementation that provides it -- see docs/dev/graphics.md.";
    if (!ctx) fatal("SDL_GL_CreateContext: %s\n%s", SDL_GetError(), kGLHelp);
    SDL_GL_SetSwapInterval(0);
    // A legacy driver still "loads": it resolves the GL 1.1 exports and leaves every
    // 2.0+ entry point null, so check the version before handing off to the renderer.
    int glver = gl_load_with(SDL_GL_GetProcAddress);
    if (glver < GCN_GL_VERSION_MIN) {
        fatal("OpenGL %d.%d is too old (got \"%s\" / \"%s\")\n%s",
              glver / 10, glver % 10, glGetString(GL_VERSION) ? (const char*)glGetString(GL_VERSION) : "?",
              glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "?", kGLHelp);
    }
    LOG(LOG_GX, "GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (gl_debug) gl_debug_enable(SDL_GL_GetProcAddress);
    if(!desktop_settings_init(win,ctx))fatal("Cannot initialize desktop settings");
    if(getenv("GCN_SETTINGS_OPEN"))desktop_settings_show();
    // Beside the memory card: both are state this machine accumulates for this game.
    plat_make_dirs("saves");
    gx::render_set_shader_cache("saves/shaders.bin");
#ifdef GCN_OPENXR
    // --vr: the headset takes over from here, the window staying up as a mirror.
    if (vr) {
        const int rc = xr_desktop_main({win, pump_events, merge_desktop_pad}, iso);
        record::finish();
        input_log_end();
        input_map_shutdown();
        threads_request_quit();
        plat_exit_now(rc);
    }
#endif
    gx::render_init(scale);
    if (g_eye_mode) eye_init();
    if (first_person) gx::render_set_first_person(true);
    if (gx::g_dump_dir) plat_make_dirs(gx::g_dump_dir);

    audio_open();

    uint32_t entry = boot_load(iso);
    threads_start_boot(entry);

    bool running = true;
    uint32_t frames = 0;
    const float theater_stereo_m = getenv("GCN_THEATER_STEREO") ? (float)atof(getenv("GCN_THEATER_STEREO")) : 0.0f;
    FILE* present_trace=[] {
        const char* path=getenv("GCN_PRESENT_TRACE");if(!path)return (FILE*)nullptr;
        FILE* file=fopen(path,"w");if(!file)fatal("Cannot open presentation trace");
        fprintf(file,"frame,host_ns,events_ns,wait_ns,render_ns,overlay_ns,swap_ns,trace_ns,pace_requested_ns,pace_elapsed_ns\n");return file;
    }();
    const bool frame_stats=getenv("GCN_FRAME_STATS")!=nullptr;
    const bool profile_host=present_trace || frame_stats;
    PresentationStats presentation_stats;
    uint64_t event_ns=0,wait_ns=0,render_ns=0,trace_ns=0;
    auto last_pace=clock_pace_profile();
    auto t0 = std::chrono::steady_clock::now();
    while (running) {
        uint64_t mark=profile_host?host_ns():0;
        running = pump_events(win);
        update_pad();
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        gx::render_set_window_size(dw, dh);
        if(profile_host){const auto now=host_ns();event_ns+=now-mark;mark=now;}
        auto b = gx::take_batch(4);
        if(profile_host){const auto now=host_ns();wait_ns+=now-mark;mark=now;}
        if (!b) {
            // Refresh the host UI even when the guest is waiting for network input.
            if(desktop_stats_idle(dw,dh,desktop_settings_open() || display_settings_hint())) {desktop_settings_draw();SDL_GL_SwapWindow(win);}
            if(profile_host)event_ns+=host_ns()-mark;
            continue;
        }
        if (b->control) { b->control();if(profile_host)render_ns+=host_ns()-mark;continue; }
        if(!g_eye_mode && theater_stereo_m<=0.0f) {
            const unsigned ui_width=display_ui_render_width();
            const int ui_scale=ui_width && dw>0 ? int((unsigned(dw)+ui_width-1)/ui_width) : scale;
            gx::render_set_internal_scale(std::max(scale,ui_scale));
        }
        // GCN_STEREOLOG=1: what the game's stereo hook (vr_game.h) would have a headset
        // show, whenever the answer changes, numbered as the frame dumps are.
        static const bool stereolog = getenv("GCN_STEREOLOG") != nullptr;
        if (stereolog && vr::game_hooks().wants_stereo) {
            static int last = -1;
            const int want = vr::game_hooks().wants_stereo(*b) ? 1 : 0;
            if (want != last) {
                last = want;
                diagnostic_log( "[stereo] frame %u: %s\n", gx::present_count(), want ? "stereo" : "theater");
            }
        }
        bool presented;
        if (g_eye_mode) {
            float P[16], V[16], H[16];
            eye_matrices(P, V, H);
            gx::render_set_vr_eye(P, V, H);
            presented = gx::render_execute_eye(*b, g_eye_fbo, kEyeW, kEyeH, true);
            static uint32_t eye_frames = 0;
            eye_frames++;
            if (gx::g_dump_dir && gx::g_dump_every && eye_frames % gx::g_dump_every == 0) eye_dump(eye_frames);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_eye_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, kEyeW, kEyeH, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        } else if (theater_stereo_m > 0.0f &&
                   (!vr::game_hooks().wants_panel_pair || vr::game_hooks().wants_panel_pair(*b))) {
            // GCN_THEATER_STEREO=<metres>: the flat frame as the stereo pair a headset
            // would hang on its panel, the eyes that far apart; the window shows the right
            // eye's, and the frame dumps come in pairs (_l, _r). The panel is the headset
            // frontend's, 3.2 m wide (kQuadW there), in the game's units. A frame the game
            // wants flat (wants_panel_pair) is drawn flat, as the headset does.
            static const float u = [] {
                VrConfig d;
                if (vr::game_hooks().config_defaults) vr::game_hooks().config_defaults(d);
                gx::render_set_panel_band(d.panel_band);
                return d.units_per_metre;
            }();
            presented = gx::render_execute_stereo_pair(*b, 0, 0, theater_stereo_m * u, 3.2f * u);
        } else {
            presented = gx::render_execute(*b);
        }
        if(profile_host){const auto now=host_ns();render_ns+=now-mark;mark=now;}
        if (presented) {
            // macOS (GL on Metal) only presents correctly with the window framebuffer bound.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            desktop_stats_present(dw,dh);
            desktop_settings_draw();
            if (dump_output && gx::g_dump_dir && gx::g_dump_every &&
                gx::present_count() % gx::g_dump_every == 0) {
                std::vector<uint8_t> pixels((size_t)dw * dh * 4), flipped(pixels.size());
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int row = 0; row < dh; ++row)
                    memcpy(flipped.data() + (size_t)row * dw * 4,
                           pixels.data() + (size_t)(dh - 1 - row) * dw * 4, (size_t)dw * 4);
                char path[512];
                snprintf(path, sizeof(path), "%s/output_%05u.png", gx::g_dump_dir, gx::present_count());
                write_png(path, flipped.data(), dw, dh);
            }
            if (record::active()) {
                int rw, rh;
                SDL_GL_GetDrawableSize(win, &rw, &rh);
                record::frame(0, rw, rh);
            }
            const uint64_t swap_began=profile_host?host_ns():0;
            SDL_GL_SwapWindow(win);
            // Optional timing evidence counts only newly rendered game frames,
            // never overlay refreshes or hidden corrective rendering.
            if(profile_host) {
                const auto now=host_ns();const auto pace=clock_pace_profile();
                if(present_trace) {fprintf(present_trace,"%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",gx::present_count(),
                    (unsigned long long)now,(unsigned long long)event_ns,(unsigned long long)wait_ns,
                    (unsigned long long)render_ns,(unsigned long long)(swap_began-mark),(unsigned long long)(now-swap_began),
                    (unsigned long long)trace_ns,(unsigned long long)(pace.requested_ns-last_pace.requested_ns),
                    (unsigned long long)(pace.elapsed_ns-last_pace.elapsed_ns));
                fflush(present_trace);}
                if(frame_stats) {
                    const double overrun=std::max(0.,(double(pace.elapsed_ns-last_pace.elapsed_ns)-double(pace.requested_ns-last_pace.requested_ns))/1e6);
                    auto summary=presentation_stats.frame(now,{event_ns/1e6,wait_ns/1e6,render_ns/1e6,(swap_began-mark)/1e6,(now-swap_began)/1e6,trace_ns/1e6,overrun});
                    if(summary)diagnostic_log("[present-pacing] host_ns=%llu frames=120 fps=%.2f p95_ms=%.2f p99_ms=%.2f max_ms=%.2f over25=%u events_max_ms=%.2f wait_max_ms=%.2f render_max_ms=%.2f overlay_max_ms=%.2f swap_max_ms=%.2f trace_max_ms=%.2f pace_overrun_max_ms=%.2f\n",
                        (unsigned long long)now,summary->fps,summary->p95,summary->p99,summary->maximum,summary->over25,
                        summary->maximum_work.events,summary->maximum_work.wait,summary->maximum_work.render,summary->maximum_work.overlay,
                        summary->maximum_work.swap,summary->maximum_work.trace,summary->maximum_work.pace_overrun);
                }
                trace_ns=host_ns()-now;event_ns=wait_ns=render_ns=0;last_pace=pace;
            }
            if (frame_limit && gx::present_count() >= frame_limit) {
                diagnostic_log( "[exit] frame limit %u reached\n", frame_limit);
                running = false;
            }
            frames++;
            auto now = std::chrono::steady_clock::now();
            double secs = std::chrono::duration<double>(now - t0).count();
            if (secs >= 2.0) {
                char title[128];
                snprintf(title, sizeof(title), GCN_APP_LABEL " - %.1f fps", frames / secs);
                SDL_SetWindowTitle(win, title);
                frames = 0;
                t0 = now;
            }
        }
    }
    desktop_settings_shutdown();
    desktop_stats_shutdown();
    record::finish();
    input_log_end();
    input_map_shutdown();
    desktop_gamepad_shutdown();
    threads_request_quit();
    plat_exit_now(0);
}
