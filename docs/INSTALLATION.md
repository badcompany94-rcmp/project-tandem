# Install and update Project Tandem

## First installation

Download the Windows full package from Releases. Extract the entire ZIP before running anything, into a folder where your Windows user can write files. Run `Setup-Tandem.cmd`, select your own supported USA `GM4E01` ISO/CISO, and wait for the five setup stages to finish.

Setup verifies the packaged files and the disc's `main.dol` SHA-1, caches a private copy of your ISO under `local-data`, and compiles the native game. The progress display covers verification, disc checking, configuration, compilation, and final checks. First setup can take several minutes and substantial disk space.

Run `Launch-Tandem.cmd` after setup. To play without starting the online services, use:

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

## Updating an existing installation

1. Close Tandem and allow automatic online cleanup to finish.
2. Open the cumulative update ZIP and copy **all** contents into the existing folder containing `Launch-Tandem.cmd`.
3. Accept file replacement and keep `local-data` intact.
4. Run `Launch-Tandem.cmd`. It rebuilds the local game if the version changed.

Cumulative updates support Alpha12 and later portable installs directly. Intermediate patches are unnecessary. For an older install missing the portable tools, extract the full package over the existing game folder instead.

Saves, controller mappings, display/sound settings, and the cached disc stay under `local-data`. The cache lets later updates work even if your original ISO moves. If a folder was moved and its build cache contains old absolute paths, setup preserves the old cache and rebuilds at the new location.

## What to share

Share the official full package or cumulative update ZIP. Your own installed folder contains private game data and session files; it is not a redistributable installer. Keep `local-data` and your disc image private.
