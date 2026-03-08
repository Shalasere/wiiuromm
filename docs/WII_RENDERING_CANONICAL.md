# Canonical Wii App Rendering (Research Summary)

## Scope
This document captures the canonical, source-backed ways to render Wii homebrew UI and graphics, then maps those options to this repository.

## Canonical Rendering Paths

1. `libogc + GX` (canonical low-level path)
- Primary official stack for performant Wii rendering.
- Typical flow: initialize VIDEO first, then initialize GX/FIFO.
- Best when you need strict control over frame timing, batching, and memory layout.

2. `VIDEO + console_init` (text-mode/debug path)
- Canonical for diagnostic text, early boot visibility, and fallback UI.
- Lowest complexity; limited visual fidelity.

3. `GX wrappers/frameworks` (productivity path)
- `GRRLIB`: friendly 2D/3D wrapper over GX.
- `libwiigui`: structured GUI toolkit on top of GX, using PNGU + FreeTypeGX.
- `libwiisprite`: sprite-focused GX abstraction.
- Best when building GUI-heavy apps quickly with lower GX boilerplate.

## Canonical Boot/Render Lifecycle

1. Initialize video mode and framebuffer (`VIDEO_Init`, configure preferred mode, set framebuffer, `VIDEO_WaitVSync`).
2. Initialize input (`WPAD_Init`, `PAD_Init`).
3. If using GX: allocate/align FIFO, call `GX_Init`, configure clear/copy state.
4. Main loop: scan input -> update app state -> render -> present/sync (`VIDEO_WaitVSync`).
5. Keep render state mutation on one thread; use message passing if worker threads are used.

## Selection Matrix

- Choose `VIDEO+console` when:
  - diagnostics-first app,
  - minimal dependencies,
  - fast bring-up.

- Choose `libogc+GX` when:
  - need animation/sprites/texture pipelines,
  - want predictable performance and scaling.

- Choose `GRRLIB/libwiigui` when:
  - need richer GUI quickly,
  - prefer higher-level API and widgets over raw GX.

## Recommended Path For This Repo

1. Keep current shared core (`core/`) state machine and platform glue split.
2. Define renderer interface with two Wii backends:
   - `ConsoleRenderer` (existing path),
   - `GXRenderer` (new target path).
3. Move only presentation concerns to renderer (header/footer, list layout, colors, progress badges).
4. Keep networking/downloader/state logic renderer-agnostic.
5. Add frame-time and dropped-frame diagnostics in the render backend.

## Risks / Gotchas

- Heap pressure from texture/font assets on Wii is tight; preload policy matters.
- Threaded render updates without strict synchronization can cause non-deterministic UI bugs.
- Wrapper libs improve velocity but can constrain custom rendering behavior later.

## Sources

- devkitPro `libogc/GX` wiki: https://devkitpro.org/wiki/libogc/GX
- devkitPro Wii examples repository: https://github.com/devkitPro/wii-examples
- GRRLIB docs: https://grrlib.github.io/GRRLIB/
- libwiigui overview: https://wiibrew.org/wiki/Libwiigui
- libwiigui tutorial: https://wiibrew.org/wiki/Libwiigui/tutorial
- libwiisprite overview: https://wiibrew.org/wiki/Libwiisprite
- FreeTypeGX overview: https://wiibrew.org/wiki/FreeTypeGX
