# Native Direct3D 11 desktop backend (private experimental build)

The same executable supports OpenGL (default) and Direct3D 11 (experimental).
F1 > Display > Graphics API saves the choice in renderer.cfg; restart to apply.
Discard restores the previous choice. Missing/invalid preferences default to
OpenGL. Failed native device creation falls back to OpenGL without replacing the
saved preference. Renderer choice does not change simulation, networking,
content validation or the executable's online compatibility identity.

Direct3D 11 hardware feature level 11_0 / shader model 5 is the Windows target.
Flip-discard presentation falls back to flip-sequential or legacy discard where
required. Software rendering is not selected automatically. Native device,
resource, pipeline and draw submission use D3D; no OpenGL context or ANGLE is used
in native desktop mode. A different API does not guarantee better FPS.

## Developer build and dependencies

Set TANDEM_NATIVE_D3D11=ON, TANDEM_GLSLANG_SOURCE_DIR and
TANDEM_SPIRV_CROSS_SOURCE_DIR to these official source checkouts:

- glslang 16.6.0: e1b562a8bed273a02f30b59b66a5d499793cede5
- SPIRV-Cross vulkan-sdk-1.4.363.0: f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7

Alternatively set TANDEM_D3D11_COMPILER_PACKAGE_DIR to a verified host compiler
archive made by tools/package_d3d11_compiler.py. Player builds then need no
Khronos source downloads or toolchain compilation. Its manifest checks archive
SHA256, compiler/target ABI and canonical LF source hashes; stale or corrupted
packages fail with repair diagnostics. This archive contains host support code,
not a disc image or generated game functions. Both upstream license files travel
with it. The merged archive is about76MB before package compression in the tested
RelWithDebInfo build; slim toolchains may retain debug information.

Dependencies are not fetched automatically. Shared GLSL compiles to SPIR-V, HLSL,
then DXBC through Windows D3DCompile. SPIR-V is an intermediate representation;
no Vulkan runtime is required. The tested Windows executable links Khronos and
C++ libraries statically. Release builds and notice packaging remain gates.
Retain glslang's multi-license and SPIRV-Cross's Apache-2.0 notices.

The unmodified Dear ImGui DX11 backend comes from upstream v1.92.9b,
f1cc2ae15e53a861a874c3034aae6798fde194ab, matching existing ImGui.
Its MIT license remains in gcn-recomp/third_party/imgui/LICENSE.txt.
Backend SHA256:
cpp 749d12b01011f504284497db21cb52b3c6f73c59612e7115f46f41e0c4ea1c49
h fb8a314a6a2904dcc5bfeb938a169389e170f233ad367d33f1ce5c7bde586a7c

Default builds retain TANDEM_NATIVE_D3D11=OFF until release gates pass.
OpenGL-only builds require neither Khronos dependency. CPU benchmark/runtime
source lists exclude native implementation modules and SDL desktop owners.

## Implementation

The native device owns hardware context, swap chain, resize/minimize and window
target. The GX renderer consumes shared batches: packed geometry, decoded mips,
reflected TEV/flat uniforms, depth/blend/logic/raster/samplers and EFB copies.
Offscreen clip-Y inversion and clockwise native front faces preserve GL raw EFB
rows. D32_FLOAT matches GL DEPTH_COMPONENT32F. Opaque-black fallback matches GL's
unbound texture value. Point geometry preserves programmable size/coordinates.
Lines remain one native pixel wide, matching GL's current lack of glLineWidth.

Shared copy shaders/tile encoders preserve CPU-visible copies. Scanout assembles
partial XFB strips and aspect/letterboxing. Checkpoints retain EFB/depth and
immutable textures, with copy-on-write for later EFB writes. A bounded GPU target
pool is warmed at initialization; an independent CPU oracle verifies resource
preservation in tests. The existing texture replacement callback supplies RGBA
and native mip generation; actual HD-pack coverage remains pending.

Window/presentation, F1 and statistics dispatch to the active API. Native ImGui
uses SDL2/DX11. OpenGL retains existing backends. VSync and optional FPS use common
settings. Native interpolation is currently disabled in F1, preserving the saved
OpenGL rate. VR, eye/theater and CLI recording launches use OpenGL and preserve
the renderer preference. Communicate these limitations before public delivery.
Internal EFB resolution stays fixed for an online process because render rollback
checkpoints require matching sizes. The F1 control explains this and is disabled
until a new session; window/display changes remain available. Offline scale
changes preserve EFB RGBA with linear sampling and depth with nearest sampling,
decoded replacement textures and the existing in-memory shader cache.

## Shader cache and startup pacing

Native DXBC/reflection lives separately in saves/shaders.bin.d3d11. GL is read
only for known keys; its driver binaries/identity are not used, and Direct3D never
rewrites it. Native files are bounded, checksummed and atomically replaced.
Lengths, metadata and source/compiler fingerprints are checked. Stale records
recompile; bad files preserve working memory entries. Device-rejected cached
programs are invalidated/rebuilt. Saves run after startup warming/at exit, not
per rendered frame.

Remembered pipelines warm before simulation starts in time-budgeted steps, with
visible progress/event pumping. A single compile can exceed the step budget.
New-course/cold misses still compile synchronously before submission; this is a
remaining frame-pacing concern, not a solved-stutter claim.

An exact-source vertex compiler cache reuses DXBC and reflection only after the
generated HLSL matches. Its 32-entry / 8 MiB bound is thread-local. On the same
442-pair corpus it reduced CPU compilation from 13.893 to 5.788 seconds with
identical compiled bytecode hashes. This is compiler work, not an FPS result.

A subsequent controlled hidden menu run compared the installed private build
with the compiler cache and experimental binding cache candidate (native scale,
960x720, VSync off, identical scripted input and isolated cards). Cold full-run
FPS was 58.45 -> 59.48, p95 17.97 -> 17.95 ms, p99 19.14 -> 18.49 ms, maximum
479.68 -> 216.83 ms, and intervals over 50 ms 13 -> 6. Warm full-run FPS was
59.76 -> 59.80, p95 17.86 -> 17.99 ms, p99 18.55 -> 18.47 ms, maximum
139.22 -> 139.52 ms. A roughly 139 ms transition remains with negligible render
work. The cold 217 ms stall also remains unacceptable for a smoothness claim.
This single paired run on an RTX 3070 Ti is initial evidence, not a release gate
or proof for older hardware. Exact binaries and all samples are retained in the
private validation/menu-pacing-v3-02 directory. No outliers were removed.

F1's Display tab offers a Very Low preset: native internal resolution,
interpolation off, HD textures off. It preserves VSync, renderer choice, texture
quality preference and engine rules. Existing preview/save/discard behavior
applies, and the preset is disabled during online sessions where EFB size is
fixed. This reduces optional rendering load; it does not change simulation speed
or claim to solve CPU/first-use compilation stalls. Both renderers passed a
hidden F1 visual/boot check with this control.

An actual hidden boot converted 442 GL keys in 13,050 ms. The next launch restored
all 442 with zero compilation in 114 ms on the development RTX 3070 Ti. These are
startup observations, not race performance or promises for other hardware.

## Validation and remaining release gates

TANDEM_D3D11_TESTS enables device/preference fixtures. TANDEM_D3D11_SHADER_TESTS
also enables compiler/library/program, cache, GX and GL/native parity fixtures.
GPU tests use hidden windows only with no competing GPU workload. Use local
caches privately; do not redistribute them as test assets.

Passed fixtures cover all 16 logic ops (hardware/forced 11_0 fallback), packed
geometry/textures, uniform shadowing, viewport/scissor/culling/depth, 1,280 copy
pixel comparisons, 760 tiled CPU writes, seven point widths, 42 directional line
cases, partial clears/XFBs, letterboxing, z-freeze, snapshot COW/restoration and
cache corruption/source invalidation. Live native signatures compile across443
locally observed GX pairs. Three textured point-coordinate cases also match GL.
Native replacement upload/mips and 1x-to-2x RGBA/depth/resource preservation pass.
Both APIs boot real game/F1 in one executable; default
OpenGL runtime compilation passes. Same-state whole-game parity is not yet
established: unsynchronized boot images differ in animation phase. Native stock
Grand Prix, two-player splitscreen and Plus retro-course gameplay captures have
been inspected. Clean
mixed-renderer1440-tick races in both host directions, D3D/D3D and GL/GL controls
retain equal confirmed traces/checksums. Synthetic25ms one-way,5ms jitter,2%loss
and100ms bursts also synchronize, including actual native rollback/correction.
These same-PC tests use production garage/LAN fallback or a local mesh router;
they do not test authenticated physical EOS delivery.

Required before promotion/release:

- Broader offline cup/layout, actual HD-pack and visible resolution/VSync
  coverage beyond the native device/replacement fixtures.
- Longer mixed-renderer race/layout and disconnect/reconnect coverage.
- Average FPS and p95/p99/max race/full presentation intervals, retaining startup
  and correction spikes and preserving the OpenGL baseline.
- Older AMD/Intel/NVIDIA and physical Internet/NAT/relay checks.
- Dependency notices, release build/launcher packaging, settings/save/pack
  preservation and candidate identity/manifest verification.

Local tests do not establish physical Internet reliability. Main/live-test copies
remain untouched while required stability gates are open.
