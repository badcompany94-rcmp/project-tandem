# Graphical launcher preview

The Windows launcher preview provides Play, setup/repair, update downloads,
help/controls and local logs in one window. It shows the package/build needed
for online compatibility and retains the credited POOTERMAN artwork. Play
calls the existing unified LAN/Internet launcher; F1 settings remain in game.

Setup runs with the existing bundled compiler/tools, reuses the private cached
disc when available, and keeps saves/settings. Child jobs run in the background
with progress/error output retained under `local-data/launcher-logs`. Errors
stay visible in the window. Leave the window open while a task finishes.

**Update downloads opens GitHub.** This first preview does not download or
install updates automatically. Apply the complete cumulative ZIP as usual,
preserving `local-data`. There is no mod manager or course editor in this preview.

`Build-Launcher.ps1 -Root <portable folder>` compiles the small authored
`ProjectTandem.exe` bootstrap with Windows' .NET Framework compiler. It contains
no game code. The bootstrap opens `Tandem-Launcher.ps1` without a console window.
The existing `Launch-Tandem.cmd` remains a direct-launch fallback. This preview
is Windows-only; a native Deck/Linux graphical front end and controller
navigation remain follow-up work.
