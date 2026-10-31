# Private LAN preview

This Windows preview is private and experimental. It has not been published as a release. Everyone in a room needs the same private build ID. Public Beta 5 and the existing Steam Deck test build cannot join this preview.

## Host and join

1. Open **Launch-LAN.cmd** from the Tandem installation. It starts the same game in LAN mode without an EOS login or an Internet connection.
2. Choose **LAN Play**, then **Host Room**. Share the eight-character room code.
3. Other players on the same local network open **Launch-LAN.cmd**, choose **LAN Play → Join Room**, and enter that code.
4. The game searches the local network for that invitation. Seating, readiness, character/kart selection, Grand Prix/Battle, and return to the same lobby use the existing game flow.

This is automatic discovery of a matching room, not an automatic switch from Internet play. The normal launcher continues to use EOS Internet play.

If discovery does not find the host, open **Join-LAN-by-IP.cmd**, enter the host's local IPv4 address, then join using the room code. A typical address is `192.168.x.x` or `10.x.x.x`. All players use the same LAN port (default 28122).

Use a trusted private network. Allow the LAN helper on that Windows private network if the firewall asks. Guest Wi-Fi/client isolation can prevent devices from reaching one another; an IP address cannot bypass that isolation. No router port forwarding is needed. IPv4 is supported in this first preview; IPv6-only networks and native Linux/Deck LAN support remain pending.

## Transport and testing

The helper exchanges reliable lobby/handshake messages over local TCP. Racing inputs use direct UDP between participants with the existing redundant-input recovery, checksums, prediction and rollback. The game process continues to use loopback sockets. The host controls room membership; LAN admission and per-room random credentials reject unrelated packets. This local transport is unencrypted and is not an Internet hosting service.

Current coverage includes eight helper processes exchanging matching inputs, discovery, mismatched-build rejection, leave/rejoin and host closure, plus a real native Grand Prix through two LAN helpers. Those are same-PC tests. Two physical PCs, Wi-Fi loss/isolation, different adapters and long sessions still need testing.

## Counter and experimental pacing

The top-right FPS/network counter defaults off. Enable it in **F1 → Display → Show FPS/network counter (F10)**, or press **F10**. The choice is saved in `saves/overlay.cfg`; Discard restores the opening choice. Existing display settings are preserved separately. The optional overlay includes RTT, actual input buffering, RTT jitter and route type when available.

Stable pacing remains the default. **Launch-Responsive-Networking.cmd** selects an experimental Internet profile that drains excess buffering faster after recovery. It retains the measured delivery floor and poor-link fallback, and does not skip changed inputs to reduce delay. This does not guarantee lower latency on every connection. For a LAN comparison, the PowerShell launcher also accepts `-Lan -NetworkProfile Responsive`.

VSync and high-refresh interpolation remain independently switchable and off by default on fresh settings.
