# Preserving Time Trial leaderboards during releases

Game build identity authorizes a supported client. It is not a leaderboard reset policy. Routine releases must keep public times and downloadable ghosts on their existing compatible board.

The service maps supported builds to a stable board ID. The stock original-course board is `mkdd-stock-tt-v1`. Beta 14 (`f9eabc32e1c80230`) and Beta 15 (`a2dd119d8cd41286`) are mapped to it. Multiplayer's matching-build requirement is unchanged.

These are mandatory release gates. Do not publish a release or change its service mapping if preservation checks fail or existing records become inaccessible. Record the evidence with the release validation.

Before adding a release:

1. Review changes to physics, course data, recording interpretation and replay timing. Menu, launcher or renderer-only changes do not by themselves justify a new leaderboard.
2. Confirm prior ghosts remain valid, with preserved course, time and input bytes, and check playback when simulation or recording paths change.
3. Add the supported build to the existing board's release mapping. Keep its stable ID and legacy namespace list. Removing an obsolete client from the access allowlist must not remove its historical records from the board.
4. Capture a read-only pre-change inventory and verified ghost copies before any storage or compatibility-mapping change. Retain a recoverable backup before any database migration. Verify after the change that prior entries remain visible and their original IDs still download the same ghosts. Compare names, times, timestamps and ghost hashes; check duplicate-upload handling and private/public isolation. The top-50 UI alone is not a complete inventory if more records exist.
5. Keep private test uploads separate. Do not delete public records or replace the board just to produce a clean release screenshot.

The November 4 service patch reads Beta 14/15 legacy records in place and stores new runs under the stable board namespace. It does not rewrite or delete old recordings. Re-uploading the same identity's identical recording retains the original receipt and timestamp. Historical duplicate copies count once in the displayed list while their existing download IDs remain valid. Distinct runs and players remain separate, subject to the existing top-50 display and storage limits.

If a future gameplay change is genuinely incompatible, preserve the old category and plan how players can access it before changing the release mapping. Never silently reset the standings. The downloaded Beta 14/15 archive documentation predates this service correction; the current guide and release notes describe the live behavior.
