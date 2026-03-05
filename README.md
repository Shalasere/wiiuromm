# wii(u)romm

New project scaffold for a RomM-style client targeting Wii U first, with a parallel Wii/vWii target.

`switchromm` was left unchanged.

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

Fast dev loops:

```bash
./scripts/dev-wiiu.sh   # build + run in Cemu
./scripts/dev-wii.sh    # build + run in Dolphin
```

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

- Extract shared core logic from `switchromm` into `core/` in this repo.
- Keep platform glue (input/fs/net/render) target-specific.
