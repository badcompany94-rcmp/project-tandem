# Public Beta 13 — experimental Direct3D 11

This update adds a native Direct3D 11 renderer for Windows, alongside OpenGL. OpenGL remains the default.

- In **F1 → Display → Graphics renderer**, choose **Direct3D 11 (experimental)**, save, then restart the game. Switch back to OpenGL the same way. Unsupported Direct3D hardware falls back to OpenGL.
- **Apply Very Low preset** sets internal resolution to 1× and turns high-refresh interpolation off. It previews immediately offline, supports Save/Discard, and keeps your VSync choice. Choose internal resolution before joining online.
- Direct3D remembers compiled shaders between launches. Identical vertex shaders reuse compilation work; the compiler test corpus produced identical bytecode with less startup compilation time.
- Players using OpenGL and Direct3D can play together **on the same release/build**. Renderer choice does not change gameplay or the online compatibility identity.

Direct3D is experimental. First-use shader compilation and scene transitions can still hitch, and performance on older AMD/Intel hardware has not yet been verified. This is not a promise of 60 FPS on every computer. Direct3D currently targets the original 60 FPS; high-refresh interpolation, VR and CLI recording use OpenGL.

**Existing players:** use Check for updates in the launcher, or extract the cumulative update into the existing game folder. The first update adds the native shader compiler support files and rebuilds the game locally. Saves, controls and your private disc-image cache are preserved.

**New players:** extract the full Windows ZIP, then run Setup-Tandem.cmd and select your supported USA Double Dash disc image. The package contains no disc image, extracted assets or generated game executable.

If you are troubleshooting low FPS, compare the same course at 1× with each renderer, then share **F1 → Display → Copy graphics diagnostics** and your FPS observations. F10 toggles the counter.

Linux / Steam Deck: see [slex2jf's Linux build](https://github.com/slex2jf/Project-Tandem-Linux).

Private Online Time Trials, texture packs, course mods and custom engine classes are not part of this public update.
