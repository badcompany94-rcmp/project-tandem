# Project Tandem Public Beta 6 - Experimental Networking, LAN and 16 Players

This experimental Windows beta adds a unified LAN/Internet lobby browser and rooms for up to **16 people: eight drivers and eight passengers in eight karts**. Use the same normal launcher for all play.

## Lobby features

- Host rooms start **Private**. Switch to **Public** to list the room, or use **Lock Room** to block all new joins, including room codes, without removing current members.
- **Edit Room Name** changes the lobby name with keyboard or controller input. Names initially use the host's name; duplicates receive a numbered suffix.
- The original Join Room keypad now includes **Browse Public Lobbies**. Search updates while typing; sort names A-Z/Z-A, player count, or ping in either direction. Nearby LAN rooms appear first and duplicate LAN/Internet listings are combined.
- The browser shows people as **N/16**, actual measured ping when available, and clear full/locked/in-progress/build status. It refreshes automatically while preserving your search and selection; joining can be cancelled.
- LAN connections use direct local input delivery. Internet play continues to use EOS direct connections and relay fallback. A host can connect a mixed group through the same room.
- Room code, members, seats and lobby settings remain together between events. Ready state is preserved when renaming or changing privacy/lock settings.

## Defaults and artwork

The FPS/network counter is **OFF on fresh settings**. Enable it in **F1 > Display > Show FPS/network counter** or press **F10**. Existing saved choices, controls, saves, trigger settings, VSync and interpolation choices are preserved. Stable networking pacing remains the default. VSync and high-refresh interpolation remain independent, experimental and OFF by default on fresh settings.

The application and optional **Project Tandem.lnk** shortcut use artwork by **[POOTERMAN](https://www.deviantart.com/pooterman)**, contributed in [issue #11](https://github.com/badcompany94-rcmp/project-tandem/issues/11). Thank you for this awesome artwork!

## Compatibility and updating

**Everyone must use this exact build: `db77d19767b75cad`.** Beta 5 and earlier builds cannot join Beta 6 rooms. The existing Linux/Steam Deck test package has not been updated for this networking build.

New players: extract the full Windows ZIP, run Setup-Tandem.cmd and select your own supported USA GM4E01 disc image. Existing Alpha12-or-newer portable installs: close the game, copy **all** files from the cumulative update into the folder containing Launch-Tandem.cmd, replace files and preserve local-data. No intermediate updates are required. Setup rebuilds locally and creates the optional artwork shortcut.

## Validation and remaining limits

- Room/roster, native UI, browser, 16-player LAN and mixed-route helper tests pass. Sixteen actual native clients completed 1,440 confirmed ticks each with identical complete input/checksum traces and returned to the same lobby.
- Sixteen-peer simulations pass with 20% random packet loss, burst loss, jitter, reordering and duplicate packets. The normal installed two-client LAN test remains about 60 FPS with matching state and no rollbacks.
- Real EOS directory publishing, atomic name reservation/rename and offline fallback pass. Local LAN browser tests cover measured ping and discovery. Full independent-device EOS joining and ping have not been verified in this update; both local SDK processes use the same device identity.
- **Separate-PC/router/Wi-Fi behavior, longer Internet sessions and 16 physical clients still need community testing.** Running 16 clients on one PC was a functional stress test and does not establish 16-player frame pacing or latency on real connections. No claim of guaranteed zero latency or LAN 0 ms is made.
- LAN uses IPv4 and requires devices to reach one another. Guest Wi-Fi isolation and private-network firewall rules can prevent discovery. LAN transport is intended for a trusted local network. Unknown ping remains Measuring; submillisecond measured ping displays <1 ms.

Previous Beta 5 downloads remain available. This is a public **pre-release**, not a guarantee that every Internet or LAN setup has been validated. Downloads contain no disc image, extracted game data, generated game source, compiled game, saves, login cache, or developer account credentials. The existing public EOS game-client configuration and bundled setup tools are retained.
