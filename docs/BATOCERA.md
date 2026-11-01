# Batocera / Wine launch paths

Issue #10 reports a Windows installation launched through Batocera returning
"No game image found". Directly launching `mkdd.exe` bypasses the PowerShell
launcher and its `local-data/settings.json`. Older executables search their
Windows build-time path and `rom/` relative to the frontend's working folder.

The follow-up executable discovers the portable cached image next to the
installation, or `rom/` beside the executable/package, independently of the
working folder. ISO/CISO and uppercase extensions are supported. Explicit image
arguments take priority and an invalid explicit path fails without silently
choosing a different disc. `--print-game-image` prints the selected image and
exits without starting a game. Missing-image errors list the checked locations.

For an existing Windows build, prepare it on Windows with your own supported
USA disc image first. Copy the complete installed folder, including your private
`local-data/game-image.iso` or `.ciso`, to
`/userdata/roms/windows/Project Tandem.pc/` on your own Batocera device. Do not
upload or share that installed folder; public ZIPs contain no disc or game.

Following [Batocera's Windows/Wine instructions](https://wiki.batocera.org/systems:windows),
an `autorun.cmd` in the `.pc` folder can explicitly set the working directory
and image even for an older executable:

```ini
DIR=local-data/game
CMD=../build-portable/mkdd.exe ../game-image.iso --no-input-log --widescreen --fullscreen --scale=2
```

Use `../game-image.ciso` instead if your cached disc is CISO. This is Batocera's
`DIR`/`CMD` configuration syntax, not a Windows batch script. The game working
folder keeps saves under `local-data/game/saves`. Keep its spelling/case exact
on Linux. Editing Windows `settings.json` does not configure this direct launch.

This direct-executable example is for **offline play**; it does not start the
Windows networking helper or shutdown watcher. Native Linux/Steam Deck and Wine
online support need separate validation. Windows relocation tests cover the new
disc resolver; real Batocera/Wine hardware testing remains pending.
