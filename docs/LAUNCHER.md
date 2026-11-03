# Project Tandem launcher and automatic updates

## One-file installation

The standalone **ProjectTandem.exe** release download includes the first-install
window. Choose an installation folder, click **Install**, and select your own
supported USA ISO/CISO after the package downloads. Progress stays visible while
the game builds. No ZIP extraction or separate setup script is needed.

The completion screen offers a desktop shortcut and an optional Steam game-picker
step. The desktop shortcut points to the installed launcher and preserves another
Tandem installation's existing shortcut. Steam requires its own final confirmation;
the installer copies the launcher path for Browse and opens
`steam://open/addnonsteamgame`. It does not write Steam's shortcut database.

**Open Launcher** finishes setup. The downloaded EXE remembers the installation
using `%LOCALAPPDATA%/ProjectTandem/installed-root.txt`; the launcher inside the
installation always prefers its own folder. This marker contains only the path.
Installer logs remain in `local-data/installer/logs`. An interrupted download can
be retried in the same chosen folder. A failed disc check or build can be retried
with another image. A Windows job contains the install worker and its compiler
children so they stop if the installer process is terminated.

Optional `paths.json` / `portable.txt` preferences control user-data locations;
existing installations keep today's layout. See [user data](USER-DATA.md).
Moving or renaming a current installation no longer triggers a native rebuild.
Matching gameplay builds are reused. When a native update is needed, a stationary
compiler cache is backed up for rollback and reused for incremental compilation;
unchanged generated game sources keep their timestamps. A moved compiler cache
still needs replacement when an actual code update or Repair requires a build.

On Windows, open **Project Tandem.lnk** or **ProjectTandem.exe** after setup.
Play starts the same unified LAN/Internet game; game settings remain in F1.
The launcher shows the installed release and online build ID and credits
POOTERMAN's artwork. Beta 9 assigns the launcher window its own Windows taskbar
identity and the fan-art icon, so it is represented as Tandem rather than its
PowerShell host. Taskbar relaunches open ProjectTandem.exe.

## Updating

The launcher checks the official GitHub releases at startup and every 15 minutes
while open. **Check for updates** runs a check immediately. Automatic checks can
be disabled with the checkbox; manual checks still work. Published public betas
are included, because Tandem's player releases are currently betas. Drafts,
source archives and unrelated Linux/Deck packages are excluded. A connection
failure leaves the existing game playable and provides a retryable status.

When a newer Windows release is available, read **What's new**, close the game,
then click **Install update**. Installation is not forced while playing.
From Beta 10 onward, the launcher stays open until Windows confirms that the
update progress window is visible. The update window is brought forward and
stays open through downloading, verification, installation and any local build.
If it cannot open, the launcher stays available and reports the failure. The
launcher closes only after that handoff, so its files can be replaced safely.
Older releases still use their previous updater when installing this hotfix;
the visibility fix applies to updates started after Beta 10 is installed.

The updater:

1. Confirms the selected official release, downloads its cumulative package and
   checks the GitHub SHA-256 asset digest. Missing or changed bundled tools cause
   a full-package repair download instead.
2. Checks archive paths, the release version and every packaged file hash before
   replacing installation files. Private data cannot be supplied by the archive.
3. Backs up changed managed files and, when needed, the existing native build.
4. Installs the files and rebuilds locally from the cached supported disc if the
   gameplay build changed. A matching native build is reused. A disc download,
   another compiler installation and manual ZIP extraction are unnecessary.
5. Verifies the resulting package/build and reopens the launcher, ready to play.

Download cancellation keeps the installed game. Once installation begins, leave
the progress window open. A failed install/build restores the prior managed
files, build and settings. Reopening ProjectTandem.exe after an interruption
runs recovery before the normal launcher. A Windows job contains the build's
compiler processes so they stop if the updater is interrupted.

Saves, control bindings and display/audio choices stay under **local-data**.
Unmanaged personal files are retained. Only obsolete files owned by the previous
package manifest are removed; their backups are kept with the update journal.
Update backups/logs are private and can consume additional disk space. The
updater checks available space before unpacking and building.

## Setup, repair and fallback

**Set up / repair** uses the bundled tools and cached disc; first setup still
requires your own supported USA GM4E01 ISO/CISO. Progress and errors are retained
under local-data/launcher-logs and local-data/updates. **Open logs** locates local
diagnostics. Review excerpts before sharing: update folders can include private
build backups and must not be uploaded as a whole.

**Launch-Tandem.cmd** remains the direct game-launch fallback. Full and
cumulative ZIP updates remain available on GitHub for manual installation.
Beta 7 and earlier launchers need one manual update/setup to receive this updater;
subsequent supported Windows releases can be installed inside the launcher.

The authored ProjectTandem.exe bootstrap is downloadable on its own and is also
compiled locally with Windows' .NET Framework compiler. It contains the small
installer scripts and artwork, with no disc assets or recompiled game code. Compilation
replaces the launcher atomically and reuses an unchanged bootstrap during repair.
This launcher/updater is Windows-only. Native Deck/Linux updating, a mod manager
and a course editor are not implemented by this release.
