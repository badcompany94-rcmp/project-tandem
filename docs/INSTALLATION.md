# Install and update Project Tandem

## Linux community build

For Linux, see **[slex2jf’s Project Tandem Linux](https://github.com/slex2jf/Project-Tandem-Linux)**. This is a separately maintained community adaptation; follow the instructions in that repository. Windows/Linux online compatibility has not yet been verified.

## First installation on Windows

Download **ProjectTandem.exe** from Releases. This single file opens the installer without a ZIP or separate setup script. Choose a writable installation folder (use **Browse** to select another drive), then click **Install**. It downloads the latest official Windows release, checks the archive and file hashes, and asks for your own supported USA `GM4E01` ISO/CISO. Keep the progress window open while it builds the game.

At completion, choose **Create a desktop shortcut** if wanted. **Add to Steam (finish in Steam)** copies the installed launcher path and opens Steam's **Add a Non-Steam Game** picker. Click Browse, paste that path into File name, then Open and **Add Selected Programs**. Steam shortcuts launch Tandem; updates still come through the Tandem launcher. This step does not edit Steam's database or restart Steam.

Click **Open Launcher** to reach the normal Play/update window. The original downloaded EXE also remembers the installed location. If you move or delete that folder, open the launcher inside its new location or use a fresh installation folder.

For manual installation, the full Windows ZIP remains available. Extract it completely into a writable folder and run `Setup-Tandem.cmd`.

**Reinstalling with existing saves:** keep the old installation's `local-data`
folder, then choose that same installation folder in the single-file installer.
It recognizes retained data, shows an existing-save notice, and keeps saves in
`local-data/game/saves` for the reinstalled game. Other installed game files must
have been removed; use the normal launcher to update or repair a working copy.
If you configured an external data folder, also retain `paths.json` (and any
`portable.txt`) and `local-data/installation-id.txt`; existing ownership checks
still apply. The installer does not search other folders or drives for saves.

Setup verifies the packaged files and the disc's `main.dol` SHA-1, caches a private copy of your ISO under `local-data`, and compiles the native game. The progress display covers verification, disc checking, configuration, compilation, and final checks. First setup can take several minutes and substantial disk space.

After setup, open **Project Tandem.lnk** or **ProjectTandem.exe** for the
[Windows launcher with automatic updates](LAUNCHER.md). Its Play button starts the same
unified LAN/Internet game. `Launch-Tandem.cmd` also launches directly.
To play without starting online services, use:

```powershell
.\Launch-Tandem.cmd -Offline
```

The full player package contains Python, CMake, Ninja, LLVM-MinGW, SDL sources, and the runtime dependencies it needs. Separate Visual Studio or Windows SDK installation is unnecessary.

## Supported image

| Field | Required value |
| --- | --- |
| Region | USA |
| Disc ID | `GM4E01` |
| Input | Uncompressed ISO or CISO |
| `main.dol` SHA-1 | `e389e50357d4a5076ab3d38672204658f411123d` |

The hash identifies the executable extracted from the disc, not the whole ISO. Use a compatible image dumped from your own disc. No disc image, extracted game executable, generated game source, or compiled game is included in the download.

## Updating in the launcher (Beta 8 onward)

Open ProjectTandem.exe or Project Tandem.lnk. The launcher checks automatically;
you can also choose **Check for updates**. When offered a newer release, close
the game and click **Install update**. Download, verification, installation and
any needed cached-disc rebuild happen automatically. The launcher then reopens.
See [automatic updates and recovery](LAUNCHER.md). Saves and settings are kept.

## Manual update / getting the updater for the first time

1. Close Tandem and allow automatic online cleanup to finish.
2. Open the cumulative update ZIP and copy **all** contents into the existing folder containing `Launch-Tandem.cmd`.
3. Accept file replacement and keep `local-data` intact.
4. Run `Launch-Tandem.cmd` once. It rebuilds the local game if the version changed and creates the graphical launcher. You can then use Project Tandem.lnk or ProjectTandem.exe. Beta 8 adds automatic updates for subsequent supported releases.

**Update everyone before playing online.** Every member of a room must run the same build ID; different builds cannot join or play together. Controller settings, trigger click points and display choices do not need to match.

Cumulative updates support Alpha12 and later portable installs directly. Intermediate patches are unnecessary. For an older install missing the portable tools, extract the full package over the existing game folder instead.

Saves, controller mappings, display/sound settings, and the cached disc stay under `local-data`. The cache lets later updates work even if your original ISO moves. If a folder was moved and its build cache contains old absolute paths, setup preserves the old cache and rebuilds at the new location.

## What to share

Share the official standalone **ProjectTandem.exe**, full package or cumulative update ZIP. Your own installed folder contains private game data and session files; it is not a redistributable installer. Keep `local-data` and your disc image private.
