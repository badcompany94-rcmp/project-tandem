# LAN and public lobby browser

Public Beta 8 retains experimental unified LAN/Internet rooms. Everyone needs build `a0cfea16f09d7018`; Beta 7 uses that same build. Beta 6 and earlier builds, including the existing Steam Deck test build, cannot join. See [release validation and remaining limits](releases/public-beta-8.md).

## Host and join

1. Open your normal **Launch-Tandem.cmd** or existing Project Tandem shortcut. One launcher combines LAN and Internet rooms. If Internet login is unavailable, LAN remains available.
2. Choose **Online Play > Host Room**. Rooms start **Private**; share the eight-character code to admit friends. Switch to **Public** to appear in the browser.
3. Use **Edit Room Name** above the karts to change the lobby name. Type up to 24 characters or use the controller keyboard, then **Save Room Name**. Back cancels. The initial name is your player name; duplicate names receive `(1)`, `(2)`, and so on. Renaming keeps members, seats, ready status and the invitation code.
4. Other players choose **Join Room** and enter the code, or choose **Browse Public Lobbies**. Nearby LAN rooms appear first, with duplicate Internet entries combined. Search updates as you type; sort by name, player count or measured ping. Counts mean human players, **N/16**, including people sharing a kart. Rooms support up to **16 people: eight drivers and eight passengers**. Up to eight people may drive solo; additional players share a driver's kart.
5. The host can switch Public/Private at any time. Private hides the listing while retaining code joins. **Lock Room** blocks all new joins, including codes; connected players remain. Unlock admits players again. Returning from an event preserves the same room.

The browser refreshes automatically and preserves your search, sorting and selected lobby. Unknown ping displays Measuring; a measured result under one millisecond displays <1 ms. A stale, full, locked, in-progress or incompatible room cannot bypass host admission checks. All players use the same LAN port (default 28122).

Use a trusted private network. Allow the LAN helper on that Windows private network if the firewall asks. Guest Wi-Fi/client isolation can prevent devices from reaching one another; an IP address cannot bypass that isolation. No router port forwarding is needed. IPv4 is supported in this first preview; IPv6-only networks and native Linux/Deck LAN support remain pending.

## Transport and testing

The helper exchanges reliable lobby/handshake messages over local TCP. Racing inputs use direct UDP between participants with the existing redundant-input recovery, checksums, prediction and rollback. The game process continues to use loopback sockets. The host controls room membership; LAN admission and per-room random credentials reject unrelated packets. This local transport is unencrypted and is not an Internet hosting service.

Current coverage includes sixteen helper processes exchanging matching inputs, discovery, mismatched-build rejection, leave/rejoin and host closure, plus a real native Grand Prix through two LAN helpers. Those are same-PC tests. Two physical PCs, Wi-Fi loss/isolation, different adapters and long sessions still need testing.

## Counter and experimental pacing

The top-right FPS/network counter defaults off. Enable it in **F1 → Display → Show FPS/network counter (F10)**, or press **F10**. The choice is saved in `saves/overlay.cfg`; Discard restores the opening choice. Existing display settings are preserved separately. The optional overlay includes RTT, actual input buffering, RTT jitter and route type when available.

Stable pacing remains the default. The optional PowerShell launcher argument `-NetworkProfile Responsive` selects an experimental Internet profile that drains excess buffering faster after recovery. It retains the measured delivery floor and poor-link fallback, and does not skip changed inputs to reduce delay. This does not guarantee lower latency on every connection. The normal launcher keeps the Stable profile; no separate player launchers are needed.

VSync and high-refresh interpolation remain independently switchable and off by default on fresh settings.
