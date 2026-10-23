# Troubleshooting and bug reports

## Setup or first launch

**A file-selection dialog seems missing:** check behind other windows. The setup progress display remains visible while setup waits for your ISO selection.

**Unsupported disc:** setup accepts the USA `GM4E01` executable hash documented in the README. A different region, revision, modified executable, or damaged image can fail verification.

**Package file missing or changed:** extract the complete official ZIP again. Setup verifies the manifest before it runs the build tools. Do not run the launcher from inside the ZIP.

**Configured ISO moved:** rerun setup and select your own compatible image. Current setup keeps a private cache so future updates can reuse it without the original path.

**Windows blocks a downloaded file:** check that the ZIP came from the project's official release and compare its SHA-256 with the release checksums. Do not disable antivirus or install an unverified replacement binary to bypass a warning. An unsigned beta may be unfamiliar to Windows; report the exact warning.

## Multiplayer

**Cannot join:** compare the version and short build ID shown in each player's lobby/F1 menu, then verify the room code. Every player must finish the same update and any automatic rebuild.

**Online login fails:** check Internet access and whether the network blocks EOS traffic. Try launching offline with `Launch-Tandem.cmd -Offline` to distinguish a connection problem from a game/setup problem.

**Stutter or input delay:** toggle F10 and note FPS, RTT, and BUF on every PC. Include player count, host, approximate regions, solo/shared kart arrangement, and whether the problem occurs while racing or during introductions/results. High RTT, network jitter, and CPU/GPU load can have different effects.

**A closed game leaves files busy:** allow its automatic cleanup to finish. Report a helper that remains running rather than terminating unrelated Python or PowerShell processes.

## What to include in a report

- Version and build ID from the lobby or F1.
- Windows version, CPU/GPU, and controller model when relevant.
- Mode, course/arena, player count, solo/shared seats, and exact reproduction steps.
- FPS/RTT/BUF readings from the affected PCs for an online issue.
- Whether it also happens offline, and whether all players see it.

Setup writes `local-data/setup.log`; individual game sessions write diagnostics under `local-data/sessions`. Review excerpts before posting. Logs can contain local file paths, player identifiers, room information, or session credentials.

Do not attach your ISO, the entire installed folder, `local-data`, game saves, generated source, compiled game, `config/eos.json`, or files containing `token`, `admission`, or private session/configuration values. The bug-report form does not require a whole-folder upload. Start with reproduction steps and counter readings; provide reviewed excerpts if needed.
