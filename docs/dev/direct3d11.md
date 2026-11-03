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

The shader generator has an opt-in interface for backend-managed point expansion.
It exports point size and coordinates explicitly and fixes varying locations,
including optional fog/depth inputs. OpenGL callers use the unchanged default
source. Native pixel shaders require actual interpolated point coordinates, not
the shader compiler's constant-coordinate compatibility substitution. A native
geometry expansion stage is still required.

For developer compiler validation, set `TANDEM_D3D11_SHADER_TESTS=ON`,
`TANDEM_GLSLANG_SOURCE_DIR` and `TANDEM_SPIRV_CROSS_SOURCE_DIR`, then build
`d3d11_shader_corpus_test`. Reference dependency commits are glslang 16.6.0
`e1b562a8bed273a02f30b59b66a5d499793cede5` and SPIRV-Cross
`vulkan-sdk-1.4.363.0`, `f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7`.
These optional developer tests do not download or package dependencies.

Pass a local `saves/shaders.bin` path followed by `points` to compile the native
variants and check the live Direct3D stage signatures through shader reflection.
Use `hash` to record the default OpenGL source hashes. The fixture never edits
the input cache. Do not redistribute local shader caches as test assets. Compiler
and signature checks do not prove pixel equivalence, rollback, or mixed-renderer
online behavior.

`graphics/d3d11_shaders` is the reusable CPU compiler module behind the native
backend. It produces Direct3D bytecode, original uniform names with explicit
buffer offsets/array and matrix strides, and texture/palette slot bindings. It
validates live stage signatures and only replaces a valid compiled program after
both stages succeed. Compilation needs no OpenGL context. Runtime integration
must use a verified bytecode cache or prepare shaders outside active race draws.

The same optional compiler-test configuration builds `d3d11_shader_library_test`.
Pass the local shader cache path to validate generated programs and their reflected
layouts, malformed-source/configuration failure preservation, and explicit target
orientation options. Orientation compilation is not a pixel-parity test.

`graphics/d3d11_program` creates native vertex/pixel shaders and reflected constant
buffers. Cached uniform slots avoid string searches per draw; a CPU shadow skips
uploads when values are unchanged. Array and matrix updates retain their reflected
strides. Failed initialization or invalid updates preserve a working program.

With compiler tests configured, `d3d11_program_test` checks these layouts through
actual GPU pixels, then draws the shared GX integer TEV color pipeline with packed
vertex colors in desktop-flat and neutral-view modes. Its hidden window does not
load game assets. It does not yet cover complete GX command batches, textures,
point expansion, framebuffer copies, rollback, or mixed-renderer online play.
