Ghost Lineup, smoother menu controls, and an optional launcher close setting.

### Online Time Trials
- Ghost Lineup makes racing other players' ghosts explicit: every entry has a **Race Ghost** button beside the player's name and time.
- Mouse hover and controller focus highlight the button only. Up/down moves between buttons; press A to race. Y opens course selection, X refreshes, and left/right changes pages.
- Pressing Race Ghost gives that button a pressed appearance while downloading, without flashing the entire column. Back still cancels.
- Select Mode, leaderboard navigation, and the post-race menu now use deliberate stick-repeat timing and jitter protection to prevent accidental double movements.

### Controller and launcher fixes
- Fixed the control stick being ignored in menus when **Also use keyboard & mouse** was enabled. Both input methods remain usable.
- Added **Close launcher when game starts**. It is off by default, remembers your choice, and closes only after a successful launch. Setup and launch errors keep the launcher open.

### Install or update
Existing players: **Check for Updates** in the launcher, then install the update. New players: download **ProjectTandem.exe**, choose an installation folder, and select your own supported USA Double Dash disc image when prompted. Saves and settings are retained.

Everyone in a multiplayer lobby must use the same build. Time Trial boards and ghosts are build-specific; this version has its own public boards, separate from private tests and previous builds. Earlier public records are not deleted.

OpenGL remains the default; Direct3D 11 remains experimental. This is a menu/input/launcher update, with no new graphics-performance claim. HD packs, Plus/custom courses, and Extra CC development are not included.

Validation includes Johnny's controller/mouse playtests, automated production-menu and SDL input checks, ghost integrity/transfer tests, and installer/updater regression checks.
