# Project Tandem credits

## Recompiler and runtime

- **Ethan Nicholas** — the upstream [mariokartdd-recomp](https://github.com/ethannicholas/mariokartdd-recomp) project and [gcn-recomp](https://github.com/ethannicholas/gcn-recomp) recompiler/runtime. Their MIT copyright and license notices are preserved.
- **Project Tandem contributors and friend testers** — online rooms and shared kart flow, networking/prediction work, widescreen/HUD adaptations, desktop controls/settings, menu integration, portable installation, and testing.
- **decomp-toolkit contributors** — tooling used by the upstream project to describe executable layout. Function-address and symbol metadata are build inputs, not a distributed copy of the game executable.

This source snapshot vendors the runtime files used by the Windows release. `UPSTREAM.json` records the source revisions it builds on; Project Tandem includes local changes beyond those revisions.

## Libraries and tools

- **SDL** — windowing, audio, and controller support.
- **Dear ImGui** — desktop settings interface.
- **glad** — graphics API loading; notices are retained in its source headers.
- **libco / higan contributors** — the coroutine backend; its license and origin file are preserved.
- **LLVM, LLVM-MinGW, mingw-w64, Python, CMake, and Ninja** — the portable Windows build environment and its dependencies.
- **Epic Games** — Epic Online Services and the bundled redistributable EOS runtime. These proprietary components have separate terms.
- **Microsoft** — unmodified application-local Visual C++ release runtime components used by the EOS helper/runtime, under their redistribution terms.

See [third-party notices](THIRD-PARTY-NOTICES.md) and the license files supplied with each component.

## Interface artwork and font

Project-specific menu illustrations, kart/seat presentation, and the hand cursor were generated and adapted during Project Tandem development, rather than copied from the disc's asset archives. The source tree includes the assets needed for this interface. This statement describes their production method; it does not grant rights in depicted Nintendo characters or other third-party intellectual property.

The interface font is **Racing Sans One**, by **Pablo Impallari and Rodrigo Fuenzalida**, under the SIL Open Font License 1.1. Its OFL notice is retained beside the font assets. Project-generated font atlases are derived from this font.

Original game menus, characters, courses, artwork, sound, and music used while playing come from the player's own disc image. Documentation screenshots show the running game and are illustrations, not runtime asset files.

## Game and trademarks

*Mario Kart: Double Dash!!* and its original game content are Nintendo's. Project Tandem is an independent project with no Nintendo or Epic endorsement. No game ownership or trademark rights are transferred by the project license.
