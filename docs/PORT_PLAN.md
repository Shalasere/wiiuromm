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
