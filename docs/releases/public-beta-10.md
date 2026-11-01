# Project Tandem Public Beta 10 - Visible Update Progress

This launcher-only hotfix makes update progress visible. The launcher stays open
until Windows confirms that the updater window is showing, then hands over to
that window. The updater is brought forward and reports downloading, package
verification, installation and any required local build. If the progress window
cannot start, the launcher stays open with a retryable error.

The progress window stays available through all stages. Download cancellation
remains available; installation and recovery are protected against closing midway.
The fan-art icon, original launcher layout and artist credit are retained.

## Updating and compatibility

**Beta 10 and Beta 9 share gameplay build `000dbce82144c576` and can play online
together.** Beta 8 and earlier cannot join. An existing, unmoved Beta 9 installation
reuses its matching native game build; this hotfix does not change game code.

- **Beta 8 or later:** use Check for updates > Install update in the Windows launcher.
  The update that installs this hotfix still starts with your existing updater;
  the new visible-window handoff applies to updates launched from Beta 10 onward.
- **Manual/older portable installs:** close the game and launcher, extract every
  cumulative ZIP file over the existing game folder, replace files, preserve
  `local-data`, then run Launch-Tandem.cmd once. Alpha12 and later are supported.
- **New Windows players:** extract the full Windows ZIP into a writable folder,
  run Setup-Tandem.cmd and select your own supported USA GM4E01 ISO/CISO. The game
  still builds locally from your disc; no compiled copy of the game is downloaded.
- **Linux users:** see [the community Linux build by slex2jf](https://github.com/slex2jf/Project-Tandem-Linux).
  It is maintained separately. Windows/Linux online compatibility is unverified.

## Checks and limits

- 80 updater checks pass, covering verification, cancellation, preservation,
  rollback, interrupted-build recovery and detached handoff. 13 launcher checks pass.
- The actual updater window is rendered through all five progress stages, with
  native Windows visibility and a persistent window handle checked. Failed startup
  is rejected and older recovery-worker argument compatibility is covered.
- Windows launcher/UI regression checks and public source checks pass. Release
  archives are hash-audited and the cumulative update is exercised in an isolated
  installation before publication, preserving settings and the matching game build.
- The old hidden-window flag alone did not reproduce the report on the test desktop;
  the fix adds an explicit visible-window acknowledgement and foreground request.

This remains an **experimental prerelease**. Saves and preferences are retained.
VSync, interpolation and the FPS/network counter remain independent and default
OFF on fresh settings. Physical-device networking, adapter and Batocera/Wine
coverage still needs community feedback. Mods and a mod browser are not included.

Artwork: **[POOTERMAN](https://www.deviantart.com/pooterman)**. Beta 9's Linux source
portability contribution by **[malespiaut](https://github.com/malespiaut)** is retained.
Downloads exclude disc images, generated game data, compiled games, saves and
developer credentials. Previous releases remain available.
