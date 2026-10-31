# Private networking investigation

No public push, release or issue update is authorized for this work yet.

The current Internet path already uses EOS direct peer connections with relay fallback, redundant input datagrams, rollback and adaptive buffering. A change of hosting provider alone would not remove local simulation/checkpoint costs or excess queued input.

This preview separates the transport from the room/game bridge, adds LAN discovery and direct local UDP input, and exposes route type and RTT jitter. Stable pacing remains unchanged. The opt-in Responsive profile reduces excess headroom after one healthy window when more than two frames above the measured delivery floor; near that floor it retains the previous three-window recovery. Both modes preserve the original pressure response, 16-frame fallback ceiling and requirement that draining cannot skip a changed local input.

Next measurements should distinguish RTT/jitter, input queue depth, network waits, snapshot time, restore/catch-up time and slow rendering. Compare native frame-time percentiles and confirmed state, not only mean FPS. The helper's RTT jitter is variation in its independent probe RTT; it is not a packet-loss estimate.

## Server options

- Continue using EOS relay fallback while improving diagnostics and client pacing. It already handles connections that cannot establish a direct route.
- Prototype a small regional UDP relay if measured player routes justify it. Compare direct and relayed sessions before choosing locations or paying for ongoing infrastructure. A relay cannot remove geographic travel time or a slow player's simulation work.
- A dedicated authoritative race server would require a headless simulation, a state synchronization/reconciliation protocol, deployment and operational work. It is a larger architecture change than forwarding current input packets. It should also be reviewed against the project's game-data distribution boundary.
- Amazon GameLift provides managed regional game-server hosting; Agones manages dedicated game servers on Kubernetes. These host a server implementation we supply, rather than replacing the game's networking code automatically. A small measured relay pilot is the more practical first infrastructure experiment for this project.

References: [EOS P2P](https://dev.epicgames.com/docs/epic-online-services/multiplayer/nat-p2p-interface/p2p-guide/p2p-intro), [GameLift locations](https://docs.aws.amazon.com/gameliftservers/latest/developerguide/gamelift-regions.html), [Agones overview](https://agones.dev/site/docs/overview/).
