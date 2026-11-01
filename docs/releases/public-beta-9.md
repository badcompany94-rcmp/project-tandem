# Project Tandem Public Beta 9 - Linux Source Portability Fix

Thanks to **[malespiaut](https://github.com/malespiaut)** for [PR #16](https://github.com/badcompany94-rcmp/project-tandem/pull/16)!
The online menu now uses a Windows/POSIX environment-variable wrapper, fixing two
Linux compilation errors from Windows-only calls. The Windows implementation
continues to use the same calls; compiled menu instructions and relocation targets
match the previous version.

## Compatibility and updating

**Everyone in an online or LAN room must use Beta 9 build `000dbce82144c576`.**
The source change updates the compatibility ID even though the Windows menu code
is equivalent. Beta 8 and earlier cannot join this build; update the whole group.

- **Beta 8 players:** open ProjectTandem.exe / Project Tandem.lnk, choose
  **Check for updates**, then **Install update**. This update rebuilds locally
  using your cached supported disc image; allow several minutes. Saves, settings
  and controller bindings are retained.
- **Earlier portable installs:** close the game and launcher, extract every file
  from the cumulative update ZIP into the game folder, replace files and preserve
  `local-data`, then run Launch-Tandem.cmd once. Alpha12 and later are supported.
- **New Windows players:** extract the full Windows ZIP into a writable folder,
  run Setup-Tandem.cmd, select your own supported USA GM4E01 ISO/CISO and let setup
  finish. Then open ProjectTandem.exe. Separate compiler installation is unnecessary.

This is a **source portability fix**, not a packaged native Linux/Steam Deck
release. Linux launcher/updater packaging and cross-platform online compatibility
remain work in progress. The older personal Linux/Deck test build is unchanged.

## Validation and remaining limits

- Both original and patched Windows menu translation units compile with the shipped
  toolchain; disassembly and relocation targets are identical.
- Linux GCC reproduces the original two `_putenv_s` errors and passes the complete
  patched menu translation-unit syntax check. This is not full Linux runtime testing.
- Public source-boundary checks and the Python suite pass (54 passed, 10 optional
  tests skipped). Linux services and Windows launcher/updater GitHub CI pass.
- The locally rebuilt Windows game completes a hidden 1,200-frame launch, with exact
  save/settings hashes preserved. Release archives are hash-audited, and the Beta 8
  updater's package verification, cached-disc build and hidden launch are tested in
  a separate private fixture before publication.
- Existing limits remain: separate physical PCs/router/Wi-Fi/EOS conditions, 16
  physical clients, GameCube adapter hardware and Batocera/Wine need more feedback.

This remains an **experimental public prerelease**. VSync, high-refresh interpolation
and the FPS/network counter stay independently configurable and **off by default**
on fresh settings. Existing preferences are retained. Artwork remains credited to
**[POOTERMAN](https://www.deviantart.com/pooterman)**.

Downloads require your own supported disc. They contain no disc image, generated
game data, compiled copy of the game, saves or developer credentials. Previous
release downloads remain available.
