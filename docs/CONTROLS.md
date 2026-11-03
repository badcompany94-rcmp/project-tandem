# Menus, racing, and controller settings

Project Tandem has separate menu and racing controls. **The switch is automatic.** Mouse navigation works through the introductions, title screen, offline selection menus, and online menus. Names and room codes accept direct typing.

## Menu controls

| Input | Action |
| --- | --- |
| Mouse movement | Highlight the choice under the cursor |
| Left click | Select or confirm |
| Right click / Escape / Backspace | Go back |
| Arrow keys | Navigate choices |
| Enter | Confirm |
| Keyboard typing | Enter an active name or room-code field |
| F1 | Settings |

Leaving an online lobby asks for confirmation. On the online character grid, B or right-click releases your character choices for reselection. Reserved choices display player colors and names, and cannot be claimed by another player.

## Default racing controls

| Input | GameCube mapping | Typical action |
| --- | --- | --- |
| Left click | A | Accelerate |
| Right click | B | Brake/reverse |
| A / D | Main stick left/right | Steer |
| W / S | Main stick up/down | Directional stick input |
| Space | X | Use item |
| Q | Y | Use/pass item |
| E | Z | Switch riders |
| Left Shift | R trigger | Drift |
| Ctrl | L trigger | Left trigger |
| Arrow keys | D-pad | D-pad |
| Enter / Escape | Start/menu | Pause offline or open the online menu |

The game's action also depends on the rider's role and current state. Mouse C-stick movement is unbound by default and can be assigned in F1.

Race bindings remain active during results, between races in a cup, and through the award ceremony. Left click confirms those screens. Arrow keys navigate result/pause choices, Enter confirms, and Escape backs out. You do not need to swap to a controller between races.

## Mix mouse/keyboard and controller

Player one accepts mouse/keyboard and a controller interchangeably by default. Use the mouse to enter a room code, then drive with your controller. You do not need to change an input profile when switching devices.

F1 lets you override this behavior by assigning specific input devices to GameCube ports. This also supports offline local multiplayer: assign each local player to a separate port/device. Keyboard and mouse remain available as mapping options.

Xbox defaults: A=A, B=B, X=X, Y=Y, RB=Z, triggers=L/R, Menu=Start. Here **Menu** means the button with three lines and **Select** means the button with two overlapping boxes. PlayStation and Switch controllers use SDL's controller mappings; adjust them in F1 if your device reports a different layout.

## Settings and exit

In **F1 → Control mapping**, select **Port 1–4** to set independent **Left trigger click point** and **Right trigger click point** values from **10% to 100%**. Both default to **50%**. These set how far a physical controller trigger must be pulled before its assigned digital button activates, including L/R clicks or a trigger remapped to Z. Analog trigger travel is unchanged. Settings are saved per port; **Discard changes** restores the opening values, and **Reset this port's bindings** restores both click points to 50%.

Existing controller settings load with 50% defaults. New trigger settings are stored in an optional comment extension that older builds ignore, preserving their ability to read the other saved assignments and bindings. An older build uses its original trigger behavior. Advanced `controls.txt` mappings retain their own explicit thresholds.

- F1 opens controller/port assignments, racing bindings, display, sound, and online settings.
- Display settings offer common 16:9 resolutions and windowed, fullscreen, or borderless modes. Borderless uses the desktop resolution.
- Display also includes independent experimental VSync and high-refresh interpolation options, both OFF by default and switchable back off. See [display options and limits](DISPLAY-REFRESH.md).
- **F1 → Display → Internal resolution** selects Default or 1×–4× rendering. Default retains the previous launch behavior; lower explicit values reduce GPU workload independently of display resolution, including in menus. Changes preview live, save when settings close, and revert with Discard.
- **F1 → Display → Graphics diagnostics → Copy graphics diagnostics** copies the GPU, OpenGL/driver version, active internal scale, window/drawable dimensions and presentation settings for performance reports. These details also appear in the session log. A known software-renderer warning suggests checking graphics drivers; absence of that warning does not establish that a driver is working correctly.
- New installations start at 25% master volume; updates preserve existing volume.
- F10 toggles the performance overlay.
- Use F1 → Exit to desktop, Alt+F4, or the window close button. Select + Menu held together is the controller exit shortcut.
- Escape goes back or opens the appropriate in-game menu; it is not the desktop-exit shortcut.

### Experimental Windows renderer

F1 > Display > Graphics renderer offers OpenGL (default) and Direct3D 11
(experimental). Save, then restart the game to switch. Both use the same gameplay
and online build identity, so players on the same release can mix renderers.
Unsupported Direct3D hardware falls back to OpenGL. Direct3D currently runs at the
original 60 FPS target; high-refresh interpolation, VR and CLI recording use OpenGL.

For slower computers, Apply Very Low preset selects 1x internal resolution and
turns interpolation off. It previews immediately offline; Save keeps it and
Discard restores the previous settings. VSync stays as you selected it. Choose
the resolution before joining online. Performance still depends on your hardware.
