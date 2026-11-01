# Experimental display refresh

Public Beta 10 retains experimental display options on the Alpha25 gameplay baseline.
Both are OFF by default. [Release notes](releases/public-beta-10.md) describe downloads
and the remaining acceptance checks. Previous releases remain available separately.

Open **F1 → Display**:

- **VSync** reduces tearing by synchronizing swaps to the display. It defaults
  to **off** and can be turned back off independently of interpolation.
- **High-refresh interpolation** offers Off, 120, 144, 165 and 240 Hz. It defaults
  to **Off (native 60 FPS)**. Choose a rate supported by the monitor. VSync also
  limits output to the display's configured refresh rate.
- Both settings apply immediately, save when Settings closes, and restore their
  opening values with **Discard changes**. Existing display, audio, controller
  and save files retain their formats.

Interpolation is experimental. It smooths identifiable 3D geometry between native
frames and adds up to one native frame of visual delay. HUD elements and uncertain
or changing geometry retain their native updates. Scene discontinuities, renderer
restores and correction frames invalidate history. VR and partial/writeback
batches fall back to native rendering. It requires additional CPU/GPU headroom;
selecting a target does not guarantee that rate on every PC or in every scene.

The simulation, audio, input sampling and network cadence retain their original
timing. Native rendering runs once; preview geometry uses separate buffers and
framebuffer targets and never writes back into guest memory. With interpolation
off, snapshots, matching, extra renders and the high-refresh timer are bypassed.

The award/name-entry dimming panel now recognizes the displayed crop and solid
letterboxed backdrops when deciding which overlays should span widescreen. Its
vertical bounds and cinematic bars stay intact. The trophy, text, name-entry controls,
ordinary dialogs and HUD geometry keep their proportions.

## Testing and upgrade boundaries

Use Check for updates > Install update in the Beta 8 or later Windows launcher,
or extract every file from the cumulative ZIP into an existing portable installation
while preserving `local-data`. The launcher rebuilds locally from the user's own
cached disc image when needed. No generated game executable or disc data belongs
in the update archive.

All online participants need Beta 10 / Beta 9 shared build ID (`000dbce82144c576`). Different
VSync and interpolation settings do not require different builds. Returning both
settings to Off restores native presentation. Keep a separate copy if you want
to compare with an older release.

Local checks cover settings persistence and disable paths, failed driver requests,
refresh deadlines, geometry fallbacks, GPU-state isolation, native peer hashes,
and existing service/display/settings regressions. Background GPU activity makes
the recorded timing runs provisional. Clean-GPU timing, visible-monitor tearing
and smoothness/input-latency checks, and public-internet friend testing remain
pending. These options remain experimental in Public Beta 10.

Beta 3 always presents native fallback frames when interpolation is unavailable,
and avoids snapshots for unmatched geometry. See its release notes for the
4× rendering tests and remaining acceptance limits.

Beta 4 avoids redundant native cursor changes and isolates F1 cursor ownership.
See its release notes for cursor and input regression coverage.
