# Project Tandem Public Beta 5 — Trigger Click Points

Adds independent left/right trigger click-point settings requested in [issue #8](https://github.com/badcompany94-rcmp/project-tandem/issues/8), based on wodgey's proposal and initial patch.

**Online requires matching builds.** All players must update to build `890fd1f362f5b190` to play together. Different builds cannot share a room; trigger and display settings may differ.

## New settings

Open **F1 → Control mapping**, select **Port 1–4**, and adjust **Left trigger click point** or **Right trigger click point**. The range is **10–100%**, with **50% defaults** to preserve existing behavior. These settings control when a physical trigger bound to a digital button activates, including L/R clicks and a trigger remapped to Z. Analog trigger output is unchanged.

Each port saves its own values. **Discard changes** restores the values from when settings opened. **Reset this port's bindings** restores both click points to 50% along with that port's other defaults.

Existing controller files load with 50% defaults. New settings use a backward-compatible comment extension: Beta 4 can still read assignments and bindings, while ignoring the added trigger values. The proposed plain `trigger` entry is also accepted by the new loader. Advanced `controls.txt` mappings retain their own explicit thresholds.

## Validation

- 695 checks through four real SDL virtual controllers: left/right independence, every port, range endpoints including 100%, remapped digital actions, unchanged analog travel, persistence and invalid input.
- Actual SDL UI events cover sliders, port switching, Save/reload, Discard and Reset. The new files were read with both the new loader and a separately compiled Beta 4 loader.
- Existing gamepad, controller routing, settings, display, presentation, interpolation and room-service checks pass. The Beta 4 cursor regression still passes all 31 checks.
- Two native Grand Prix peers confirm 1,200 ticks with matching state and stable native presentation at 1080p/4×. The installed package is rebuilt and smoke-tested before publication.

Tests use virtual controllers and hidden windows on one Windows PC. Controller-specific firmware behavior and actual Internet friend testing still need user reports. Please report remaining issues with controller model, port, mappings and click points.

The Beta 4 cursor stutter fix and corrected Host Room artwork remain included. **VSync and high-refresh interpolation remain experimental, independent F1 → Display options, both OFF by default.** Updates preserve saved choices and existing saves.

## Downloads and updating

- **New installation:** `Project-Tandem-Windows-Public-Beta-5.zip`. Extract fully, run `Setup-Tandem.cmd`, select your own compatible USA GM4E01 ISO/CISO, then run `Launch-Tandem.cmd`.
- **Existing Alpha12+ / Beta installations:** `Project-Tandem-Public-Beta-5-Cumulative-Update.zip`. Close the game, copy all contents over the folder containing `Launch-Tandem.cmd`, replace files, and keep `local-data`. Launch normally to rebuild from your cached image. Saves and settings are preserved.
- `SHA256SUMS.txt` contains archive checksums. GitHub's automatic source ZIPs are for development.

**Build compatibility: `890fd1f362f5b190`**, based on Alpha25 gameplay. Everyone in an online room must use this matching build; trigger settings and display options may differ. Beta 4 and previous releases remain available separately.

Downloads contain no disc image, extracted game data, generated game source, compiled game, saves, device login cache or developer account credentials. Bundled setup tools and the existing public EOS client configuration are retained.
