# New System Handoff

Purpose: reproducible startup point and continuation plan for `wiiuromm`.

## 1) Bootstrap On A Fresh System

```bash
git clone https://github.com/shalasere/wiiuromm.git
cd wiiuromm
sudo pacman -S --needed devkitppc wut wiiu-dev libogc
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
```

Optional emulator setup:

```bash
./scripts/install-cemu-local.sh
./scripts/install-dolphin-arch.sh
```

## 2) Minimum Health Check

```bash
make -C tests test
make -C tests integration
make
make -C wii
make validate
```

Expected: all host tests green, both targets build, runtime smoke passes.

## 3) Current Project State

- Shared `core/` is active on both Wii U and Wii targets.
- API/config/downloader/session/logger paths are implemented.
- Runtime harness and emulator smoke flow are wired.
- Remaining parity work is primarily native transport + downloader finalize/resume hardening.

## 4) Where To Continue (Priority Order)

1. Replace host-oriented HTTP integration shim with native Wii + Wii U transport.
2. Match downloader finalize semantics with switch reference behavior.
3. Tighten resume integrity (manifest/hash validation + partial resume correctness).
4. Add UX parity gaps (badges, richer diagnostics, queue failure summary).
5. Run full gates: `make -C tests test`, `make runtime`, `make validate`, `make validate-visible`.

## 5) Security/Secrets Policy

- Do not commit real credentials or tokens in source/tests.
- Use environment variables (`SERVER_URL`, `USERNAME`, `PASSWORD`, `API_TOKEN`) for local runs.
- Before commit:

```bash
rg -n 'password|token|secret|api[_-]?key|authorization' tests core source wii/source
git diff --staged
```

## 6) Operational Commands (Daily Loop)

```bash
./scripts/dev-wiiu.sh
./scripts/dev-wii.sh
./scripts/romm-harness doctor
./scripts/romm-harness run --backend wii-dolphin --scenario smoke_boot
./scripts/romm-harness run --backend wiiu-cemu --scenario boot_menu
```

## 7) Handoff Artifact Checklist

When pausing work, update these in the next commit:

- `docs/PORT_PLAN.md` with progress + next gate.
- this file (`docs/NEW_SYSTEM_HANDOFF.md`) with exact resume command sequence.
- commit message prefix by domain: `transport:`, `downloader:`, `resume:`, `ui:`, `docs:`.
