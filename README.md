# wii(u)romm

New project scaffold for a RomM-style client targeting Wii U first, with a parallel Wii/vWii target.

`switchromm` was left unchanged.

## Start Here On A New System

- [New System Handoff](docs/NEW_SYSTEM_HANDOFF.md)
- [Port Plan](docs/PORT_PLAN.md)
- [Wii Rendering Canonical Notes](docs/WII_RENDERING_CANONICAL.md)

## Targets

- Native Wii U at repo root (`wut`, `.rpx`)
- Wii/vWii under `wii/` (`libogc`, `.dol`)

## Prerequisites

Install devkitPro packages:

- `devkitPPC`
- `wut`
- `wiiu-dev`
- `libogc`

Typical Arch command:

```bash
sudo pacman -S devkitppc wut wiiu-dev libogc
```

Set environment:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
```

Install emulators:

```bash
./scripts/install-cemu-local.sh       # local user install, no sudo
./scripts/install-dolphin-arch.sh     # Arch system install (sudo)
```

## Build

Wii U:

```bash
make
```

Wii/vWii:

```bash
make -C wii
```

Host core tests:

```bash
make -C tests test
```

Standalone API/downloader integration fixture test:

```bash
make -C tests integration
```

Full validation (host tests + emulator runtime smoke):

```bash
make validate
```

Visible emulator launch validation (opens emulator windows):

```bash
make validate-visible
```

Adjust runtime smoke timeout (seconds):

```bash
RUNTIME_TIMEOUT_SECONDS=20 make validate
```

Visible mode with custom timeout:

```bash
RUNTIME_TIMEOUT_SECONDS=20 make validate-visible
```

Fast dev loops:

```bash
./scripts/dev-wiiu.sh   # build + run in Cemu
./scripts/dev-wii.sh    # build + run in Dolphin
```

## Emulator Harness (Local Dev Loop)

Shared control harness CLI (two backend adapters):

```bash
./scripts/romm-harness doctor
./scripts/romm-harness run --backend wii-dolphin --scenario smoke_boot
./scripts/romm-harness run --backend wiiu-cemu --scenario boot_menu
```

Session artifacts are written per run under `.harness/sessions/<timestamp>/`:
`session.json`, `events.ndjson`, emulator logs, staged build, and screenshots.

## Control Conventions

Shared schema now keeps controls consistent across targets:

- Confirm (`Select`): Wii U `A`, Wii Remote `A`, GameCube `A`
- Back: Wii U `B`, Wii Remote `B`, GameCube `B`
- Queue view: Wii U `Y`, Wii Remote `1`, GameCube `Y`
- Start downloads: Wii U `X`, Wii Remote `2`, GameCube `X`
- Search: Wii U `-`, Wii Remote `-`, GameCube `R`
- Quit: Wii U `HOME/+`, Wii Remote `HOME/+`, GameCube `START`
- Diagnostics/Updater: Wii U `R/L`, GameCube `Z/L`

## Screen Model

Shared core now emits per-view screen title + hint lines for:

- Platform Browser
- ROM Browser
- ROM Detail
- Queue
- Downloading
- Diagnostics
- Updater

Screen output is now colorized with the same per-view palette as the Switch app:
PLATFORMS (blue), ROMS (teal), DETAIL (deep blue), QUEUE (purple),
DOWNLOADING (amber), DIAGNOSTICS (green), UPDATER (indigo), ERROR (red).

## Implemented Next 10

1. Search filter cycling in ROM browser (`OpenSearch`).
2. ROM sort cycling (`TitleAsc`, `SizeAsc`, `SizeDesc`) on Left/Right.
3. Empty-search-result guard (cannot open detail when no match).
4. Queue remove-selected on Left in Queue view.
5. Queue clear pending/failed on Right in Queue view.
6. Start action toggles pause/resume while downloading.
7. Deterministic simulated failure for large ROMs on first attempt.
8. Retry failed queue item with Select in Queue view.
9. Diagnostics counters (input/enqueue/dup/completed/failed/search).
10. Updater state machine (`Idle/Checking/Available/Applying/Applied`).

Notes:

- Cemu may prompt for game paths/graphics packs on first run. You can skip graphics packs.
- `dev-wiiu.sh` uses a local MLC path: `run/cemu_mlc`.
- Wii target accepts either Wii Remote `HOME` or GameCube `START` to exit.

## Package to SD

Wii U (Homebrew Launcher path `wiiu/apps/wiiuromm/`):

```bash
./scripts/package-wiiu.sh /path/to/sdroot
```

Wii/vWii (Homebrew Channel path `apps/wiiuromm/`):

```bash
./scripts/package-wii.sh /path/to/sdroot
```

## Next

- Expand shared core (`core/`) from state/queue prototype to API/config/downloader pieces.
- Keep platform glue (input/fs/net/render) target-specific.
