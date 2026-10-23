# Contributing to Project Tandem

Use Issues for reproducible problems and Pull Requests for original project/runtime changes. Keep a change focused, explain the player-visible behavior, and include the checks you ran.

## Game data stays local

Do not submit disc images, extracted game data, generated translated C, compiled game binaries, saves, or screenshots containing private identifiers. Build the game locally from your own supported disc. Keep original dependency notices and identify the origin and license of new code or assets.

Use a public alias and GitHub's `noreply` address if you want your personal email kept out of commit metadata. Git commits publish both an author name and email; account email privacy settings do not rewrite old commits.

## Development build

The easiest way to get the pinned Windows toolchain is to download and extract the official full player package. Complete its normal setup with your own ISO. Its `source` directory is a buildable source snapshot, and its `tools` directory holds the portable compiler dependencies.

To build a separate checkout with those tools, use CMake and the same compiler/configuration options documented in `distribution/Setup-Tandem.ps1`, pointing `-S` at the checkout and `-DGAME_ISO` at your own image. Keep build output outside tracked source. Different compiler versions/configuration can produce a different compatibility ID and will not join the official build's rooms.

The core game build uses `MKDD_EOS=OFF`; the official launcher runs a separate EOS helper. Rebuilding that helper uses `cmake/EOS.cmake`, `MKDD_EOS=ON`, and an EOS SDK obtained separately from Epic. Configure your own game-client product settings; production service credentials are not committed here.

Python service checks can be run with:

```powershell
python -m unittest discover -s tests -p "test*.py"
```

Runtime tests are enabled through the CMake test options. For networking changes, compare confirmed simulation state across multiple native peers and test loss/jitter as well as a clean local connection. A loopback test alone does not establish public Internet performance.

## Reporting security problems

Read [SECURITY.md](SECURITY.md). Do not include live room codes, session tokens, admission values, local configurations, or personal paths in a public reproduction.
