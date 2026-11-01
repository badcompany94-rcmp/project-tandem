# Project Tandem Public Beta 7 - Adapter and Portable Launch Fixes

This experimental Windows update implements changes for [USB GameCube adapter detection (#5)](https://github.com/badcompany94-rcmp/project-tandem/issues/5) and [Batocera disc lookup (#10)](https://github.com/badcompany94-rcmp/project-tandem/issues/10). **The reporters' physical adapter and Batocera/Wine setups have not been tested. Please retest and report whether these changes help.** Both issues remain open for confirmation.

## Changes

- **Experimental native GameCube USB adapter input on Windows.** Nintendo WUP-028 and compatible Wii U/Switch-mode adapters using USB 057e:0337 expose four individually assignable controllers in **F1 > Controller ports**. Analog triggers, digital clicks, hotplug/reconnect and rumble use the existing controller/settings path. A compatible **existing WinUSB driver is required**; Tandem does not change drivers. PC-mode adapters and ordinary USB controllers retain their SDL path. Unknown third-party protocols are not automatically supported. [Adapter guide](https://github.com/badcompany94-rcmp/project-tandem/blob/main/docs/GAMECUBE-ADAPTERS.md).
- **Portable disc discovery.** Direct launches find the cached ISO/CISO relative to the executable/package when the folder is moved or the working directory changes. Explicit image arguments take priority; missing-image diagnostics list checked paths. This targets Batocera's reported image-not-found failure; it does not establish full Wine support. [Batocera offline launch instructions](https://github.com/badcompany94-rcmp/project-tandem/blob/main/docs/BATOCERA.md).
- **Windows graphical launcher preview.** After setup, open **Project Tandem.lnk** or **ProjectTandem.exe** for Play, setup/repair, update downloads, controls and logs. Play starts the existing unified LAN/Internet game. **Update downloads opens GitHub; updates are still installed manually.** There is no mod manager, course editor, automatic updater or native Deck/Linux frontend in this preview. Launch-Tandem.cmd remains available. [Launcher guide](https://github.com/badcompany94-rcmp/project-tandem/blob/main/docs/LAUNCHER.md).

Beta 6's LAN/Internet lobby browser, editable names, privacy/lock controls and **16 people in eight karts** remain available. Networking behavior is unchanged in this follow-up. The FPS/network counter defaults off on fresh settings. VSync and high-refresh interpolation remain independent, experimental and off by default; existing saved choices are preserved.

## Install and update

**Everyone playing online must use build `a0cfea16f09d7018`.** Beta 6 and older builds cannot join Beta 7 rooms. The existing Linux/Steam Deck package has not been updated.

New players: extract the full Windows ZIP, run **Setup-Tandem.cmd**, and select your own supported USA GM4E01 ISO/CISO. Setup compiles locally and creates the launcher/shortcut.

Existing Alpha12-or-newer portable installs: close the game, extract **all** cumulative update files into the folder containing Launch-Tandem.cmd, accept replacement, and preserve **local-data**. Run Launch-Tandem.cmd once to rebuild and create the graphical launcher. Intermediate patches are unnecessary. GitHub's automatic Source code archives are not the player installer.

For Batocera, update/rebuild on Windows first, then copy your complete installed folder privately to your own device. Follow the documented offline DIR/CMD configuration. The direct executable does not start networking helpers. Do not share your installed folder or disc image.

## Validation and remaining limits

- 303 automated GameCube adapter/report/SDL checks cover four ports, assignment, buttons/sticks, analog/digital triggers, stale/unplug/reconnect input and rumble routing. Driver-registered WinUSB interface discovery is included. **Physical adapter/driver confirmation is pending**, including the unknown adapter model from #5.
- Twelve disc-lookup fixtures pass on Windows and Linux. An actual relocated Windows native game completes a hidden 1,200-frame boot from an unrelated working folder. **Real Batocera/Wine confirmation is pending.**
- Existing controller, display, presentation, optional interpolation and counter checks pass. Launcher tests cover state, quoted paths, rebuild status and visible errors; actual compiled launcher rendering and hidden Play/backend/helper cleanup pass.
- Two actual native LAN peers on this build complete 1,440 confirmed ticks with identical complete input/checksum traces, matching final state and no rollbacks at approximately 60 FPS. The installed main copy passes the same check. Fresh full-package and cumulative-update installations are rebuilt and boot-tested before publication; saved settings and private game files are preserved.
- The service/source suite passes 54 tests, with 10 opt-in integration tests skipped; separate actual native LAN tests run as described above. Beta 6's sixteen-client and simulated packet-loss coverage remains historical coverage of unchanged networking code. **Separate-device EOS/router/Wi-Fi behavior, long Internet sessions and 16 physical clients still need testing.** Local tests do not establish latency or compatibility across all hardware.

Artwork remains credited to **[POOTERMAN](https://www.deviantart.com/pooterman)**, contributed in [issue #11](https://github.com/badcompany94-rcmp/project-tandem/issues/11). Thank you for this awesome artwork!

This is a public **pre-release** for community testing. Previous Beta 6 downloads remain available. Official ZIPs contain no disc image, extracted game data, generated game source, compiled game, saves, login cache or developer credentials. The approved bundled tools and public EOS game-client configuration are unchanged.
