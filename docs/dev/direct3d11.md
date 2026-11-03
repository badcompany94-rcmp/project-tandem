# Native Direct3D 11 desktop backend (in development)

The requested release will offer OpenGL (default) and Direct3D 11 (Experimental)
in the F1 Display settings. Renderer selection will apply after restarting. It
must use shared simulation, networking, content checks and compatible release
identities; renderer choice alone must not partition online rooms. This setting
is not exposed until the native game backend works.

Direct3D 11 is the initial Windows target. Feature level 11_0 provides the
shader-model 5 baseline used by the implementation. Device creation tests actual
support; API version is not a performance guarantee. See Microsoft's
[feature-level documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro).

## Implemented foundation

`graphics/d3d11_device` owns a hardware device, immediate context, swap chain and
window render target on the render thread. It prefers flip presentation with a
legacy swap-chain fallback. It handles resizing/minimization and reports device
creation/presentation failures. It does not silently choose software rendering.
This layer does not execute gameplay or modify network state.

On Windows, configure `TANDEM_D3D11_TESTS=ON` and build `d3d11_device_test`. The
hidden test compiles native HLSL and checks exact color output, depth occlusion,
color/depth resource restoration, readback row pitch, resize/minimize, VSync modes
and preservation of a working device after invalid replacement input. Run GPU
tests only while no competing game/GPU workload is active. This is a correctness
probe, not a game performance measurement.

## Release gates still outstanding

- Native GX draws, textures, sampler state, EFB copies/readbacks and presentation.
- Correct point expansion, coordinate/depth conventions, blend and logic behavior.
- Backend-specific F1 and FPS overlays, diagnostics and persistent renderer choice.
- Native rollback snapshots and equivalent CPU-visible graphics results.
- Stock offline/cup/splitscreen/course-pack rendering and settings/save preservation.
- Mixed OpenGL/Direct3D online races, each host direction, player layouts,
  reconnect/abort and packet loss/jitter with matching simulation checksums.
- Average FPS plus p95/p99/max presentation and race intervals, including startup
  and correction spikes, on clean and impaired links. Preserve the OpenGL baseline.
- Supported older AMD/Intel/NVIDIA testing; no promised FPS gain on untested GPUs.

Shader compiler feasibility work is isolated outside the source tree. Khronos
glslang and SPIRV-Cross can compile the current shader-generation output to HLSL
and then Direct3D bytecode. This is shader compilation, not a replacement of the
native device/rendering API with an OpenGL runtime layer. Any integrated compiler
dependencies must be pinned, licensed, packaged and credited before release.
Compiler success alone does not establish coordinate, point or readback parity.
