# Project Tandem Public Beta 2 — Experimental Display Features

VSync and high-refresh interpolation are **experimental, optional, and OFF by default**. Enable either independently in **F1 → Display** and turn it back off there at any time. The previous [Public Beta 1](https://github.com/badcompany94-rcmp/project-tandem/releases/tag/v0.1.0-beta.1) remains available.

## What's new

- **Experimental VSync:** optional display synchronization to reduce tearing. Driver/display support affects the result.
- **Experimental high-refresh interpolation and frame pacing:** Off (native 60 FPS), 120, 144, 165, or 240 Hz. This smooths eligible 3D geometry between native game frames while preserving simulation, audio, input sampling, and network timing. Choose a rate supported by your monitor; VSync also limits output to its configured refresh rate.
- **Widescreen award/name-entry fix:** the gray dimming panel now covers the full widescreen view, keeping the trophy, text, name-entry controls, and cinematic bars proportioned.

Both display options apply immediately and save when Settings closes. **Discard changes** restores the values from when Settings opened. With interpolation off, its snapshots, matching, extra rendering, and high-refresh timer are bypassed.

## Experimental limits

Interpolation needs extra CPU/GPU headroom and adds up to one native frame of visual delay. HUD elements, uncertain/changing geometry, and some effects or scenes retain native updates. Selecting 120/144/165/240 Hz does **not** guarantee that output rate. Turn either option off if it causes visual or pacing problems.

Local testing covered saved enable/disable paths, driver failures, refresh scheduling, geometry fallbacks, GPU-state isolation, and matching confirmed simulation hashes across native multiplayer peers with mixed display settings. Both-off and VSync-on native timing checks stayed near 60 FPS. These hidden-window checks ran with background GPU activity: **visible-monitor tearing/smoothness/input-latency checks, clean-GPU performance acceptance, and a public Internet friend retest are still pending**. This is an experimental prerelease, not a claim of locked high-refresh performance.

## Downloads and updating

- **New player:** `Project-Tandem-Windows-Public-Beta-2.zip`. Extract completely into a writable folder, run `Setup-Tandem.cmd`, select your own compatible USA `GM4E01` ISO/CISO, then run `Launch-Tandem.cmd`. Build tools are bundled.
- **Existing Alpha12+ or Public Beta 1 portable install:** `Project-Tandem-Public-Beta-2-Cumulative-Update.zip`. Close Tandem, copy **all** ZIP contents over the folder containing `Launch-Tandem.cmd`, replace files, and preserve `local-data`. Launch normally and allow the local rebuild from your cached disc image. Intermediate updates are unnecessary.
- `SHA256SUMS.txt` contains SHA-256 checksums. GitHub's automatic source archives are for development, not the player installer.

Keep a separate copy of your current installation if you want to return to Beta 1. Disabling both options restores native presentation within Beta 2; it does not change the build version. Saves, controller bindings, and existing display/sound settings are preserved by the update.

**Compatibility ID: `4609e672bddd077e`**, based on Alpha25 gameplay. Everyone in an online room must use this matching build; VSync and interpolation choices may differ. Beta 1 and Beta 2 cannot join the same room.

**Bring your own compatible disc image.** Downloads contain no disc image, extracted game data, generated game source, compiled game, user saves, or developer account credentials. Your installed `local-data` stays private. Read the included README, controls, terms, privacy information, and third-party notices.
