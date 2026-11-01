# Play Mario Kart: Double Dash!! online with Project Tandem

Project Tandem supports rooms for **2–16 people**, with solo and shared karts in the same event. Internet rooms need an Internet connection; nearby LAN rooms can work offline through the same launcher. EOS provides device guest identities, lobby discovery, connection setup, and relay fallback; players do not need an Epic player account.

**Everyone in an online room must use the same build ID.** After updating, make sure every player updates before hosting or joining. Different builds cannot play together, even when both installations are based on the same gameplay version. Controller mappings, trigger click points, VSync and interpolation choices may differ between players.

## Start a session

1. Launch normally and choose Online Play.
2. Host a Private room and share its eight-character code, or switch it to Public. Join by code, through Browse Public Lobbies, or F1 > Online > Quick join. See the [LAN/browser guide](LAN.md) for privacy, lock, name editing, search and sorting.
3. Pick a front seat in an empty kart or a back seat behind another player. A back seat becomes available after its front seat is occupied.
4. Enter or edit your name, up to seven letters/numbers, and ready up.
5. The host chooses the mode, then everyone selects characters and karts.
6. The host chooses the cup or arena and starts the event.

All players need the same version and build ID. The lobby and F1 settings show both. Public Beta 7 uses **build `a0cfea16f09d7018`**, based on Alpha25 gameplay. Older builds cannot share a room with Beta 7. Experimental VSync/interpolation settings may differ between players.

## Characters and kart roles

The original character grid shows shared live cursors and named reservations. Your lower preview shows your own kart. A solo player chooses two characters; each person in a shared kart chooses one. Confirmed characters lock across the room. If two people confirm the same character at once, one gets it and the other can select a different character.

Use B or right-click to release your own choices and reselect. Your teammate's preview updates as your choice changes. The driver chooses the kart.

All courses, characters, karts, and mode choices are exposed in online selection, independent of offline save unlocks.

## Modes and session flow

Grand Prix fills the remaining field with CPUs. Two friends can share a kart against CPUs or drive separate karts. Battle has human-controlled opposing karts and requires at least two opposing karts; solo and shared karts can mix.

After the Grand Prix award ceremony or Battle results, the group returns to the same lobby. Change seats or modes without entering a new code. Gold, silver, and bronze award 3, 2, and 1 session stars; teammates in a shared kart both receive its medal stars.

The host can return everyone to the lobby from the online in-game menu after confirmation. An unfinished event does not award medal stars. Kicking removes a player but does not ban them: they can rejoin using the same room code. Refresh code changes the invitation while keeping current members.

## Connection and performance

Gameplay uses peer-to-peer input delivery with prediction, rollback, redundant datagrams, adaptive buffering, and automatic EOS relay fallback. You do not normally need manual port forwarding or a VPN. A managed or restrictive network may block the required traffic.

The counter defaults OFF on fresh settings. F10 toggles FPS, RTT, and BUF; the same saved option is available in F1 > Display. RTT is the network round trip; BUF is the actual queued input delay. The buffer adapts to connection conditions, prioritizing steady playback while keeping delay as low as conditions allow. A poor connection or a PC that cannot simulate fast enough can still cause delays or frame drops.

Report readings from each PC when diagnosing a problem. This update is covered by local synchronized native peers, actual EOS directory tests and simulated bad network conditions. Prior versions had external friend sessions; separate-device EOS joining, router/Wi-Fi coverage and 16 physical clients remain unverified for Beta 7. See the [release validation and limits](releases/public-beta-7.md). The public beta is intended to gather reports across more PCs and Internet connections.

Close the game normally when finished. Its owned online services stop automatically; a separate Stop-Online command is not needed.
