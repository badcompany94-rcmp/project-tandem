# Project Tandem privacy information

Project Tandem compiles the game locally from the compatible disc image you select. Setup caches a private copy under `local-data` and uses it for later builds. The disc image, extracted executable, generated source, compiled game, saves, and control settings stay on your PC during ordinary setup and play.

## Online play and Epic Online Services

The normal launcher starts Epic Online Services (EOS) and creates or reuses a device guest identity. It does not require an Epic player-account sign-in. The EOS SDK processes information needed to provide its services; Epic's [privacy policy](https://legal.epicgames.com/epicgames/privacy-policy) describes its processing.

Room membership, your chosen display name, seat, character/kart selections, readiness, session scores, and game inputs are exchanged as needed to operate the room and match. Other people in the room can see your chosen name and game selections. A room code is an invitation: anyone who receives it may attempt to join. The host can remove a player or refresh the invitation code.

Gameplay uses direct peer connections where available and Epic relay fallback when needed. Network addresses and service identifiers are used for connection setup and packet delivery. A direct peer connection can reveal a participant's public network address to the other endpoint; do not treat a room as an anonymous network connection.

You can play without starting these online services by launching with `Launch-Tandem.cmd -Offline`.

## Local diagnostics

Setup and gameplay write diagnostic files under `local-data`. These can contain local paths, timestamps, player or service identifiers, room/session information, counters, and credentials used by a current session. Tandem does not automatically attach these files to GitHub issues.

Only share reviewed excerpts needed to explain a problem. Do not upload the entire `local-data` folder, your ISO, saves, generated game files, EOS configuration, or files containing private session tokens/admission values. Information you post in a public GitHub issue is public.

The cached ISO and setup/build data remain until you remove them from your own installation. Online helper processes owned by a normal game launch stop automatically when the game closes. This does not delete your saved game or local cache.

## Questions

General questions can be raised in the project's GitHub Issues without including personal data. Questions or requests about Epic's handling of service data should use the contact and request mechanisms in Epic's privacy policy. This notice describes Tandem's current beta behavior; future releases may update it when functionality changes.
