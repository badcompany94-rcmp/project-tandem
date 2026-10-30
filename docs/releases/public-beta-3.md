# Project Tandem Public Beta 3 — Interpolation Pacing Fix

This update fixes a presentation bug reported after Beta 2: with interpolation enabled, frames that could not be interpolated could also lose their normal native presentation, causing very low or erratic displayed FPS. Native fallback frames are now always shown, and idle refresh polls cannot consume their presentation slots.

## Changes

- Preserve native presentation whenever matching geometry is unavailable, including menus, effects, scene changes and correction frames.
- Avoid GPU snapshots and extra batch copies when there is no geometry to interpolate.
- Reuse interpolation snapshot targets, including offline play, and omit unused partial-display history from full-frame snapshots.
- VSync behavior is unchanged. The widescreen award/name-entry dimming-panel fix from Beta 2 remains included.

**VSync and high-refresh interpolation remain independent, experimental options under F1 → Display, both OFF by default.** Saved choices are preserved. Interpolation can be disabled at any time. With it off, the runtime bypasses its snapshots, matching, extra rendering and high-refresh timer.

## Testing and remaining limits

Tests used the normal Windows launch configuration: 1920×1080 output and 4× internal rendering, correcting a gap in the original Beta 2 timing fixtures. Seven sequential 3,000-tick runs covered native/VSync, 120/144/165 Hz with VSync, and 120/165/240 Hz without VSync. Native timing and percentile checks passed, and all runs reached the same deterministic game-state hash. Tests also covered three real Battle peers, two Grand Prix peers with corrections and mixed display settings, independent GPU-state checks, saved enable/disable paths, and a 28,000-frame native menu/results/award route.

The opening fallback test now shows all 120 native frames; Beta 2 showed 91. This addresses dropped fallback presentation; it does **not** promise a locked 120/144/165/240 Hz output rate. Some scenes stay native. Interpolation requires extra CPU/GPU headroom and adds up to one native frame of visual delay.

These are automated hidden-window tests on one Windows PC. Visible-monitor smoothness/input-latency checks and an actual Internet friend retest remain pending. Interpolation is still experimental; please report remaining scene-specific visual or performance problems. Keep it off if it worsens performance on your PC.

## Downloads and updating

- **New installation:** `Project-Tandem-Windows-Public-Beta-3.zip`. Extract completely into a writable folder, run `Setup-Tandem.cmd`, select your own compatible USA `GM4E01` ISO/CISO, then run `Launch-Tandem.cmd`. Build tools are bundled.
- **Existing Alpha12+ / Beta 1 / Beta 2 portable installation:** `Project-Tandem-Public-Beta-3-Cumulative-Update.zip`. Close Tandem, copy **all** ZIP contents over the folder containing `Launch-Tandem.cmd`, replace files, and keep `local-data`. Launch normally; the local rebuild uses your cached disc image. Saves, bindings and existing settings are preserved.
- `SHA256SUMS.txt` contains SHA-256 checksums. GitHub's automatic source archives are for development, not the player installer.

**Build compatibility: `f4e52689263937fd`**, based on Alpha25 gameplay. Everyone in an online room must update to this matching build; display choices may differ. Older builds remain separate room versions. Previous releases remain available if you keep a separate installation.

Downloads contain no disc image, extracted game data, generated game source, compiled game, user saves or developer account credentials. Your installed `local-data` stays private. Read the included terms, privacy information and third-party notices.
