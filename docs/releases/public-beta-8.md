# Project Tandem Public Beta 8 - Automatic Windows Launcher Updates

The Windows launcher now detects new releases and installs them with one click.
Open **Project Tandem.lnk** or **ProjectTandem.exe**. The existing game, LAN/Internet
browser and experimental adapter support are retained.

## Update experience

- Checks the official GitHub releases at startup and while open, with a manual
  **Check for updates** button and an option to disable automatic checks.
- Shows a newer release, its download size and **What's new**. Click
  **Install update** after closing the game. Updates are not forced during play.
- Downloads the cumulative package, verifies SHA-256 and package file hashes,
  installs it and rebuilds locally from the cached disc if needed. Missing or
  changed bundled tools trigger a full-package repair download. A matching
  gameplay build is reused, saving rebuild time.
- Displays download/install/build progress, reopens the launcher when ready,
  and retains saves, controls and user settings. Download cancellation is safe.
- Backs up the previous managed files/build and restores them on failure.
  Reopen ProjectTandem.exe after an interrupted update to run recovery.
  Compiler children are contained so an interrupted updater stops its build.
- Offline checks fail visibly without preventing normal play. Direct CMD launch
  and manual full/cumulative ZIP updates remain available.

[Launcher, update and recovery guide](https://github.com/badcompany94-rcmp/project-tandem/blob/main/docs/LAUNCHER.md).

## Getting this first updater release

**Beta 7 and earlier need one manual update to receive automatic updating.**
Close the game, extract every cumulative ZIP file into your existing installation,
replace files and preserve local-data. Run Launch-Tandem.cmd once to finish setup.
Then use Project Tandem.lnk / ProjectTandem.exe for future updates. New players
extract the full Windows ZIP and run Setup-Tandem.cmd with their own supported
USA GM4E01 ISO/CISO. There is no game or disc download.

**Gameplay build remains `a0cfea16f09d7018`. Beta 7 is online-compatible with
Beta 8 because the gameplay build is unchanged.** Beta 6 and earlier are not.
The older native Linux/Steam Deck package has not been updated. This updater
targets Windows 10/11 x64; it does not add Linux/Deck updating, mods or a course
editor. Native adapter/Batocera hardware confirmation from Beta 7 is still pending.

## Validation and remaining limits

- 80 Windows updater checks cover numeric release ordering, public betas,
  unsupported paths, URL restrictions, corruption/truncation, cancellation,
  retained tools, active-game exclusion, rollback, detached handoff and actual
  interrupted-build recovery in a new process. Existing 13 launcher-job checks pass.
- A real official Beta 6-to-Beta 7 GitHub download installs, verifies and rebuilds
  with the cached disc, then completes a hidden 1,200-frame native boot. Save and
  controller hashes remain unchanged and custom settings survive.
- The current/available/checking/offline launcher views and actual compiled
  bootstrap are rendered and inspected. An existing Beta 7-to-Beta 8 package
  update and a fresh bundled-tool installation are verified before publication.
- Gameplay/networking code is unchanged. Previous same-build native LAN checks
  matched state/input traces at approximately 60 FPS with no rollbacks. This
  release does not establish separate-PC/Wi-Fi/EOS latency, 16 physical clients,
  real USB adapter hardware or Batocera/Wine support on untested setups.
- Updates keep private backups/logs and require free space. Do not upload your
  local-data directory or an installed folder. Release integrity is checked
  against the official GitHub asset digest and file manifest; no GitHub account
  or developer credentials are required by players.

VSync, interpolation and the FPS counter keep their existing independent saved
settings and default off on fresh settings. Artwork remains credited to
**[POOTERMAN](https://www.deviantart.com/pooterman)**. Thank you for this awesome artwork!

This remains an experimental public **pre-release**. Prior Beta 7 downloads are
preserved. Public packages exclude disc images, extracted/generated game data,
compiled games, saves and developer credentials; approved tools and EOS client
configuration remain unchanged.
