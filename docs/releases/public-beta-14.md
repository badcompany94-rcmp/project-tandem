Online Time Trials, original-style menus, and Windows frame-pacing improvements.

**November 4 service update:** Public times and ghosts now carry over between compatible releases, including Beta 14 and Beta 15. Existing records are preserved and available in both. Refresh the leaderboard; no additional download is needed. This supersedes the original build-specific leaderboard limitation below.

### New: Online Time Trials (beta)

- Open **Start Game > 1 Player > Online Time Trials**. Choose a course for your own run, or browse the course leaderboard and select a player's ghost to race.
- After selecting a ghost, choose your characters and kart, then go directly to its course. You do not need to select the course again.
- After a completed run, the persistent results menu offers **Retry, Change Name, To Leaderboards, Replay, Upload Ghost, and Quit**. Uploading is optional; the menu waits for your choice.
- Set your posting name in **Change Name** or **F1 > Online**. Uploads use your current name, including changes made during the session.
- Leaderboards refresh from the online service. Ghost downloads are checked for integrity and compatible course/build before playback. Uploads and downloads run outside the game loop.
- Public leaderboards start clean, separate from private test runs. These are community times with recording-format checks, **not competition-verified or cheat-proof rankings**. Each course displays its top 50 available runs; submission/storage limits apply during the beta.

### Menu improvements

- Three choices in the one-player mode menu: Grand Prix, Time Trials, and Online Time Trials, using original-disc artwork and native animated decorations.
- Grand Prix starts with the original flag. Select it to reveal the mushroom and CC arrows; confirm the CC to continue to character selection.
- Offline Time Trials retains its three-letter save-name selection.
- Larger original-style lettering, restored selection bands, P1 indicator and stopwatch artwork, with controller and mouse navigation.
- Cover the original mode-screen transition to prevent the old two-option screen flashing underneath.

### Frame pacing

- Use a more precise Windows wait for game pacing, reducing avoidable oversleep without busy-spinning or raising process priority.
- Detailed frame/render-queue logging is off during normal play. Use `Launch-Tandem.ps1 -Diagnostics` when collecting diagnostics.
- OpenGL remains the default. **Direct3D 11 remains experimental**, with the same online compatibility as OpenGL.

Local validation includes native menu/gameplay checks, ghost upload/list/download between separate identities, input/record validation, installer/updater checks, and prior synchronized OpenGL/Direct3D race tests for the pacing change. Loading pauses and hardware-specific stutters may still occur; further feedback is welcome.

### Install or update

**Existing players:** use Check for Updates in the launcher, then install the update. **New players:** download **ProjectTandem.exe**, choose an installation folder, and select your own compatible USA Double Dash disc image when asked. Saves and settings are retained by the updater.

All players in a multiplayer lobby must update to the same build. Time Trial ghosts and boards are also build-specific. Texture packs, custom courses and Extra CC development are not included in this release.
