# Project Tandem Public Beta 4 — Cursor Stutter Fix

Fixes redundant native cursor changes reported in [issue #6](https://github.com/badcompany94-rcmp/project-tandem/issues/6). The report described idle title/attract-mode stutter on a hybrid Intel/NVIDIA laptop. The game and the passive F1 hint could repeatedly overwrite cursor state.

## Changes

- Set the cursor shape and visibility only when the native state changes.
- Keep the passive F1 hint from controlling the cursor; interactive F1 settings take ownership while open.
- Restore the correct cursor when switching between settings and the game, including text/hover/click cursor changes.
- Preserve existing cursor visibility behavior; this fix does not add an inactivity timeout.
- Include the corrected Host Room kart illustration: tandem seats and tires face along the kart, with one steering wheel.

## Validation and limits

The regression reproduced 120 shape changes and 240 visibility changes over 120 stationary frames before the fix. After the fix, stationary menu, hint and settings sequences make zero redundant native cursor mutations. Tests cover mouse/controller transitions, hover/click behavior, gameplay hiding, and opening/reopening settings. Existing controller, settings, display, presentation, interpolation and room-service checks pass, along with hidden 1080p/4× idle/attract runs and deterministic Grand Prix/Battle peers with mixed display settings.

These are local Windows tests. The reporter's exact hybrid laptop is unavailable here; please report any remaining stutter or cursor/input problems on your hardware. The issue remains open for confirmation. Internet friend testing and visible-monitor pacing checks remain pending.

**VSync and high-refresh interpolation remain experimental, independent options under F1 → Display, both OFF by default.** Existing saved choices are preserved. The cursor fix applies automatically. Gameplay, save formats and race timing are unchanged.

## Download and update

- **New installation:** `Project-Tandem-Windows-Public-Beta-4.zip`. Extract fully into a writable folder, run `Setup-Tandem.cmd`, choose your own compatible USA GM4E01 ISO/CISO, then run `Launch-Tandem.cmd`.
- **Existing Alpha12+ / Beta installations:** `Project-Tandem-Public-Beta-4-Cumulative-Update.zip`. Close the game, copy all ZIP contents over the folder containing `Launch-Tandem.cmd`, replace files and keep `local-data`. Launch normally to rebuild from your cached disc. Saves and settings are preserved.
- `SHA256SUMS.txt` lists archive checksums. GitHub's automatic source archives are for development.

**Build compatibility: `cb95d2f1e45e147e`**, based on Alpha25 gameplay. Everyone in an online room must update to this matching build. Display settings may differ. Older releases remain available separately.

Downloads include no disc image, extracted game data, generated game code, compiled game, saves, device login cache or developer account credentials. Bundled setup tools and the existing public EOS client configuration are retained.
