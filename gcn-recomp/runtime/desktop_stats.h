#pragma once
// Call with the desktop GL context current. The overlay owns no game state.
void desktop_stats_enable(bool enabled);
void desktop_stats_toggle();
void desktop_stats_present(int width,int height);
// Redraw the last game image and refresh stale readings during a game stall.
// Returns true when the caller should swap; this is not a new game frame.
bool desktop_stats_idle(int width,int height,bool host_ui=false);
void desktop_stats_shutdown();
