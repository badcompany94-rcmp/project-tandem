# GameCube USB adapters

Public Beta 7 adds experimental a Windows WinUSB backend for Nintendo WUP-028 and
compatible Wii U/Switch-mode adapters exposing USB **057e:0337**. The four
connected pads appear individually as **GameCube USB Adapter N Port 1–4** in
**F1 > Controller ports**. Assign them there for local multiplayer. Online play
uses GameCube port 1. Buttons use their GameCube labels; both analog triggers,
their digital clicks and rumble are supported.

Use the adapter's Wii U/Switch mode if it has that switch. Windows needs a
working **WinUSB** driver for this mode, as with Dolphin's native adapter
support. Tandem does not install, replace or remove drivers. Follow the
[official Dolphin adapter guide](https://dolphin-emu.org/docs/guides/how-use-official-gc-controller-adapter-wii-u/)
for the correct adapter device; never replace the driver of an unrelated USB
device. Close Dolphin or another program using the adapter before starting
Tandem. Its second USB cable supplies rumble power; WaveBird does not rumble.

Ordinary USB GameCube controllers and adapters in PC mode continue through
SDL's existing controller mappings. They are a different path from Nintendo's
four-port adapter protocol. Unsupported vendor protocols are not automatically
covered by the native backend.

Hotplugged pads and adapters are detected while the game runs. Disconnects and
stale USB reports release input. To disable native Windows adapter probing for
troubleshooting, set `GCN_GAMECUBE_ADAPTER=0` before launch; normal SDL devices
are unaffected. Linux continues to use SDL's platform adapter support.

Report decoding, four independent SDL controllers, F1 assignments, button/stick
directions, analog/digital triggers, unplug/reconnect and rumble routing pass
automated tests. Physical adapter/driver confirmation remains pending. A passing
simulation is not proof that every third-party adapter works.
