# wiiuromm Port Plan (Initial)

- Keep `switchromm` as reference only.
- Build shared core next (`core/`) for:
  - RomM API models/parsing
  - queue/state machine
  - manifest/resume logic
  - config parsing
- Keep platform glue separate for Wii U and Wii.
