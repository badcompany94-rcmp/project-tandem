#pragma once

// Shared by desktop frontends and game-specific camera hooks. Disabled by default.
void display_set_widescreen(bool enabled);
bool display_widescreen();
// The game decides when its desktop settings hint is appropriate. The frontend
// only displays the host overlay, without reading guest addresses or state.
void display_set_settings_hint(bool visible);
bool display_settings_hint();
// Nonzero logical UI width requests enough render samples for the output width.
// Zero restores the user's normal internal scale when this UI closes.
void display_set_ui_render_width(unsigned width);
unsigned display_ui_render_width();
