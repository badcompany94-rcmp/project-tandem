# Single-file Windows launcher

Download **ProjectTandem.exe** from the Windows release. It is a small installer
and launcher, with no game data or recompiled game included.

1. Choose an installation folder and click **Install**.
2. Wait for the latest official Windows package to download and verify.
3. Select your own supported USA Double Dash ISO/CISO.
4. Keep the progress window open while the game is built locally.
5. Choose a desktop shortcut if wanted, then **Open Launcher**.

The completion screen also offers **Add to Steam (finish in Steam)**. It copies
the installed launcher path and opens Steam's own game picker. Browse to that
path and confirm **Add Selected Programs**. Steam launches the normal Tandem
launcher; Tandem continues to handle its own updates.

The originally downloaded EXE remembers the completed installation. The installed
EXE always prefers its own folder, allowing separate test installations.
Failed downloads and interrupted installation can be retried. Existing unrelated
folders and shortcuts are preserved. The installer uses the updater's official
GitHub host restrictions, SHA-256 checks, archive validation and Windows process
containment. Logs remain under `local-data/installer/logs`.

Reinstalling now recognizes retained `local-data` in the chosen installation
folder. The installer shows when an existing save folder is present and reuses
it without replacing saves. Keep only user data and any data-location preferences
when removing an old installation; use the normal launcher for an existing game.
It does not scan other drives or import saves from unrelated folders.

This is a launcher-only addition to Beta 13. It installs the existing public
game build **8434831f693c998f**; no race, renderer, physics, or networking code was
changed. Existing players can keep using their current launcher. Full and
cumulative ZIP downloads remain available.

Validation includes Windows PowerShell 5.1 installer/shortcut/recovery fixtures,
existing launcher and updater checks, actual GitHub download and clean setup
with a supported local image, the installed launcher's Play-ready check, and a
hidden game boot. Steam's game-picker route was checked against the installed
Steam client; an unattended library insertion is not claimed or performed.
