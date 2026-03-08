# wiiuromm Port Plan (Initial)

- Keep `switchromm` as reference only.
- Shared core now started in `core/` with:
  - action/view state machine used by both Wii U and Wii targets
  - queue dedupe rules (block active queue + completed history)
  - simulated download progression for UI/dev testing
- Executed next 10 features in shared core:
  - search filter cycling and ROM sort modes
  - no-match search guard for detail view
  - queue remove-selected and clear pending/failed controls
  - download pause/resume toggle
  - deterministic first-attempt large-ROM failure and retry action
  - diagnostics counters surfaced in status output
  - updater flow state machine (check/apply)
- Keep platform glue separate for Wii U and Wii.

## Next 10 (Implement + Execute + Test)

1. [x] Config loader (`config.json` + env override fallback) with schema version.
2. [x] RomM auth/session model + token validation preflight.
3. [x] Platforms API integration (replace hardcoded platforms).
4. [x] ROM list API integration (pagination + normalization).
5. [x] Queue snapshot persistence (`run/queue_state.json`) restore on boot.
6. [x] Manifest writer/reader for completed downloads.
7. [x] Resume planner (`.part` detection + contiguous resume checks).
8. [x] Real downloader worker (HTTP stream + progress callbacks).
9. [x] Error taxonomy mapping (network/auth/http/fs) to user-facing states.
10. [x] Runtime integration tests with local mock RomM server fixture.

## Execution Gates (Per Feature)

- Unit: host test for core behavior (`tests/`).
- Integration: emulator runtime smoke (`make runtime`).
- Regression: full run (`make validate` + `make validate-visible`).

## Remaining for Switch Functional Parity

1. Native Wii U/Wii HTTP transport implementation (replace host curl-based integration shim).
2. Downloader finalize parity: temp-dir promotion, multi-part/DBI archive semantics, title_id output layout.
3. Resume robustness parity: per-file manifest hash/size validation and partial-chunk resume beyond contiguous full chunks.
4. UI parity uplift: per-ROM download state badges, richer DOWNLOADING diagnostics (speed/errors), and keyboard/search UX parity.
5. Operational hardening: structured logging sink/rotation + worker/UI event channel + thread-safety audit on shared status mutations.

## Recommended Execution Hierarchy

1. Transport first: native HTTP transport for Wii U/Wii (unblocks all runtime-path parity work).
2. Download correctness: finalize semantics + title_id layout + multipart/archive behavior.
3. Resume integrity: manifest/hash validation + partial resume rules.
4. Concurrency/safety: worker->UI event channel and status mutation audit.
5. UX parity last: badges, richer DOWNLOADING diagnostics, keyboard/search polish.

## Narrowed Execution Ask (Derived from switchromm code/docs)

1. Config/API contract parity only:
   - Match `SERVER_URL`, `USERNAME`, `PASSWORD`, `PLATFORM`, `DOWNLOAD_DIR`, `HTTP_TIMEOUT_SECONDS`, `FAT32_SAFE`, `LOG_LEVEL`.
   - Match RomM routes/shape used by switch client: `/api/platforms`, `/api/roms`, `/api/search/roms`, `/api/roms/{id}`, `/api/romsfiles/{id}/content/{name}`.
2. Downloader behavior parity only:
   - Require `Content-Length`; reject chunked transfer.
   - No redirect follow.
   - FAT32 split at `0xFFFF0000`; finalize to single file or DBI-style multipart directory.
3. Resume/state parity only:
   - Manifest-backed contiguous resume from temp parts.
   - Queue snapshot restore and completed-on-disk dedupe on boot.
4. UI parity minimum:
   - Per-ROM badges: queued/downloading/completed/resumable/failed.
   - Current+overall progress, recent failure summary.

### Inputs required from user to execute accurately

1. One RomM base URL plus auth mode (basic credentials or none).
2. One stable platform slug/id to target first.
3. Expected Wii U output layout decision (`title_id` foldering: yes/no).
4. Emulator/runtime path for validation (or confirmation to keep host-only integration tests).

## Parity Delivery Plan (Wii + Wii U vs Switch)

1. Transport/API parity
   - Implement native Wii + Wii U HTTP backends.
   - Align routes with switch-compatible RomM API (`/api/platforms`, `/api/roms`, `/api/roms/{id}`, `/api/romsfiles/...`, `/api/search/roms`).
   - Exit gate: live-server catalog sync succeeds on both targets.
2. Downloader/finalize parity
   - Enforce `Content-Length`, no redirect-follow default, FAT32 split at `0xFFFF0000`.
   - Implement finalize flow parity (single-file promote, multipart DBI archive behavior, collision-safe output naming).
   - Exit gate: end-to-end download writes valid final layout on SD for Wii + Wii U.
3. Resume/state parity
   - Persist queue state + manifest state; recover pending/resumable/completed-on-disk on boot.
   - Add stronger resume validation (size + hash where available).
   - Exit gate: restart during active download resumes correctly without corruption.
4. UI/UX parity
   - Add per-ROM badges, active speed/progress/error diagnostics, queue failure summary, search parity polish.
   - Match switch screen color semantics and state hints across all views.
   - Exit gate: manual parity checklist passes on emulator for both targets.
5. Hardening + release gates
   - Add structured logs, thread-safe worker/UI event channel, and failure taxonomy wiring to UI.
   - Validate with `make -C tests test`, `make runtime`, `make validate`, `make validate-visible`.
   - Exit gate: green CI + emulator smoke + live-server Wii catalog/download smoke.
