# Project Tandem launcher and automatic updates

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
The launcher closes itself and opens an update progress window so it can safely
replace its own files. The updater:

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

The authored ProjectTandem.exe bootstrap is compiled locally with Windows'
.NET Framework compiler and contains no recompiled game code. Compilation
replaces the launcher atomically and reuses an unchanged bootstrap during repair.
This launcher/updater is Windows-only. Native Deck/Linux updating, a mod manager
and a course editor are not implemented by this release.
