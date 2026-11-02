# User-data locations

Existing installations keep their current portable layout. Nothing is moved
automatically. These options affect saves, controller/display/sound/enhancement
settings, shader cache, cached disc, sessions and user logs. Source, tools, the
compiled game and compiler cache remain inside the installation.

Place one of these files beside `ProjectTandem.exe`:

- **Neither file:** use the existing `local-data/` folder.
- **`portable.txt`:** use `local-data/`, even when `paths.json` exists.
- **`paths.json` without `portable.txt`:** use the specified data folder.

Example per-user folder:

```json
{"data":"%LOCALAPPDATA%/ProjectTandem/MyInstallation"}
```

Example folder relative to the installation:

```json
{"data":"user-data"}
```

Absolute local-drive paths also work. Relative folders follow a moved installation.
Environment variables must exist. Invalid, linked, unavailable or unwritable
locations produce an error; there is no silent fallback to another save folder.
Choose a dedicated directory, not the drive root, installation itself, or a code,
tool, mod-library or update/build directory. Separate installations need separate
data folders; an ownership record prevents accidentally sharing one.

## Preserve existing progress when opting in

1. Close the game and launcher, and keep a backup of your saves.
2. Create your chosen data folder. Move or copy the user files you wish to retain
   from `local-data/`: `game/`, `settings.json`, `game-image.json`, the matching
   `game-image.iso` or `game-image.ciso`, and launcher preferences/logs.
   Keep `build-portable/`, update journals/backups and `installation-id.txt` in the
   installation's `local-data/` folder.
3. Create `paths.json` and remove or rename `portable.txt` if present.
4. Start the launcher. An existing current build is reused; moved disc locations
   are checked without recompiling it. If no settings were transferred, run setup
   for the new location. Keep your backup until progress and controls are confirmed.

This is not a Program Files installer. Setup, native builds, launcher rebuilds,
shortcuts and updates still write to the installation and require permission to
do so. Redirecting saves alone does not remove that requirement.

## Updates and recovery

Update archives cannot supply `paths.json`, `portable.txt` or private data. Update
journals, recovery helpers and compiler backups stay inside the installation;
the existing installation path protections remain in force. Only the explicitly
named settings/cache metadata files can be backed up and restored in a configured
external data folder. Do not change paths during an update or recovery.

The first update from an older launcher uses that launcher's updater. Configure
custom data locations after installing this release, rather than expecting an old
updater to understand the new setting.
