# Modernization Program - Phase 0: Baseline & Tooling

This directory tree records the Phase 0 baseline for the Source Engine
modernization effort (see the full roadmap in the project plan):

    Phase 0  Baseline, CI gates, smoke checklist          <-- this file
    Phase 1  Legacy/dead code removal (_X360/PS3, dead modules, macros)
    Phase 2  x86 -> x64 port
    Phase 3  Graphics: remove FFP/DX6-9/togl/dx9sdk, new RHI (DX11 + Vulkan)
    Phase 4  Remove GameUI/VGUI2, modern retained-mode UI

## Repository state at baseline

- Baseline commit: `ed8209cc` ("fix build with clang-20 ...")
- `origin`   = https://github.com/mistersliva/SourceEngineClean  (our work)
- `upstream` = https://github.com/nillerusr/source-engine        (original)
- Branch: `main` tracks `origin/main`
- Tracked files: 12,382 (+ submodules `ivp`, `lib`, `thirdparty`)

## 1. Legacy-code lint (ratchet)

Script: `scripts/lint-legacy.ps1`
Baseline: `scripts/lint-baseline.json`
CI: `.github/workflows/lint.yml` (runs on every push / pull request)

The lint counts legacy-code patterns with `git grep` over all tracked
source files. Counts may never exceed the baseline; lowering a count is
encouraged and then ratcheted:

```powershell
# verify (CI mode): exit 1 if any count regressed
powershell -File scripts/lint-legacy.ps1

# after a cleanup commit that lowers counts, ratchet the baseline down
powershell -File scripts/lint-legacy.ps1 -UpdateBaseline
```

### Baseline counts (at ed8209cc)

| id                     | count | meaning / target phase |
|------------------------|------:|------------------------|
| x360_refs              |  1918 | `_X360` conditionals - Phase 1a |
| isx360_fn              |   910 | `IsX360` calls - Phase 1a |
| xbox_include           |   168 | `#include "xbox/..."` / xboxstubs - Phase 1a |
| dx_to_gl_abstraction   |   216 | fake-D3D9-over-GL define - Phase 3 |
| win32_long_no_ptr      |    83 | `Get/SetWindowLong` (truncates on x64) - Phase 2a |
| inline_asm             |   149 | `__asm` blocks (cannot compile for x64) - Phase 2a |
| d3d9_com_types         |   940 | `IDirect3D*9` types - Phase 3 |
| suspicious_ptr_cast    |    56 | pointer -> 32-bit int casts - Phase 2a |

Notes:

- `suspicious_ptr_cast` is a conservative regex (casts of `&expr` or
  `(T*)expr` to `int`, plus `reinterpret_cast<int>`). The authoritative
  gate for pointer truncation is the compiler (MSVC C4311/C4302,
  clang `-Wint-to-pointer-cast`) once the x64 build is wired in Phase 2.
- Patterns are POSIX ERE on purpose (no `\s`/`\b`) so `git grep -E`
  behaves identically on Windows, Linux and macOS runners.
- Do not put `"` characters inside patterns: PowerShell 5.1 mangles
  embedded quotes when passing arguments to native programs.

## 2. Build verification gates

Submodules are required for every build:

```powershell
git submodule init && git submodule update
```

Windows (waf):

```powershell
# 64-bit (the only target since Phase 2 removed --32bits; 32-bit used to be
# `configure -T debug --32bits` and was neither the default nor supported)
./waf.bat configure -T debug
./waf.bat build
```

Linux / macOS (existing scripts, also used by CI):

```
scripts/build-ubuntu-amd64.sh
scripts/build-macos-amd64.sh
```

Existing test scripts (also used by CI):

```
scripts/tests-ubuntu-amd64.sh
scripts/tests-macos-amd64.sh
```

Existing CI workflows: `.github/workflows/build.yml` (64-bit
Windows/Linux/macOS), `tests.yml`, and now `lint.yml`.

### Warning-count baseline (recorded — Phase 2 Stage 1)

Measured on a **clean full MSVC x64 `-T debug` rebuild at `7667fe09`**
(2026-09-23), file-redirect capture (PS 5.1-safe):

```powershell
cmd /c '.\waf.bat clean'
cmd /c '.\waf.bat build -j 8 > fullbuild.log 2>&1'
Select-String -Path fullbuild.log -Pattern 'warning [CW]\d+' |
    Measure-Object | Select-Object -ExpandProperty Count
```

| metric | count |
|---|---:|
| total `warning C/W` | **27,809** |
| C4311 (pointer truncation) — Gate 2 subtotal | **92** |
| C4302 (signed/unsigned pointer loss) — Gate 2 subtotal | **0** |
| C4312 (int → pointer of greater size) | 27,699 |
| C4291 / C4477 / C4273 / C4838 | 11 / 3 / 3 / 1 |

Root causes (scoping detail: [phase2.md](phase2.md)):

- **27,626 of the 27,699 C4312 come from one line** —
  `public/tier1/utlmemory.h:440`'s `reinterpret_cast` from `unsigned int`
  to `T *`, instantiated once per `CUtlMemory` use site. Stage 5's sweep
  collapses the total from ~27.8k to ~183 with that single fix.
- **68 of the 92 C4311s live in the pinned `ivp` submodule** (physics
  object pointers cast to `long`); the other 24 are first-party
  (`vgui2/src/InputWin32.cpp` 19, `gameui/Sys_Utils.cpp` 2, and one each
  in `voice_mixer_controls`, `baseentity`, `vguimatsurface/Input`).

Ratchet rules: the total must never exceed **27,809** and must fall as
Stages 3–5 land; **Gate 2 additionally requires the C4311+C4302 subtotal
to reach 0** on a clean full rebuild.

**Gate 2 measurement (2026-09-24).** A clean full MSVC x64 rebuild at
`6641b4ca` under `-T release` emits **11** warnings: C4311 **0**,
C4302 **0**, C4312 **1**, plus C4789 4, C4273 3, C4477 2, C4838 1 —
0 errors. The MSVC gate config is now `-T release` (retail-equivalent,
already CI-covered by `tests.yml`): the previous `-T debug` config maps
to `/Od /MTd`, and `/MTd` defines `_DEBUG`, which compiles Valve assert
dialogs in — the first worker thread then trips
`tier0/minidump.cpp:423` (ReThrow wrapper, `threadtools.inl`) and blocks
boot on any Windows debug build, so debug cannot be the *running* config
until a follow-up `fix:` commit removes that blocker. `-T debug`
(`19` warnings at Gate 5) remains what `build.yml` compiles. The 27,809
ceiling still holds.

## 3. Runtime smoke checklist (manual, requires game content)

The engine repository alone cannot boot; a content mod (e.g. Half-Life 2)
must be mounted with `gameinfo.txt`. Run this checklist at every phase gate
and record results (date, build config, pass/fail, notes):

1. Cold boot to main menu (no `Error()` popup, no missing-material spam).
2. `mat_queue_mode 2` + `developer 1`, then restart - still boots.
3. Load a map: `map d1_trainstation_01` from console (`-toconsole`).
4. In-game: render target, flashlight (dynamic light), suit HUD visible.
5. `vid_restart` (or resolution change) - device recovers.
6. Save + load a game (save/load exercises serialization - critical for
   the x64 phase where struct sizes may change).
7. Options menu opens/closes; video mode list populates.
8. Alt-tab / window-fullscreen toggle x5, no crash.
9. `net_graph 1` + console open while moving - no input loss.
10. Clean quit, no hang.

Phase 2 additions:

11. Process is 64-bit (Task Manager / `dumpbin /headers` shows machine 8664).
12. Save files from the 32-bit build fail fast: loading one prints the
    tailored "written by a 32-bit build" warning at the header check and
    aborts - no partial load, no stream desync. 32-bit saves are
    incompatible by design and there is no migration path; the audit and
    rationale are documented in `phase2.md` Stage 6.

Phase 3 additions:

13. Backend reports `D3D11` / `Vulkan` (new RHI backend name) instead of DX9/GL.
14. Screenshot comparison vs the DX9 baseline for: `d1_trainstation_01`
    (worldcraft props), `d2_coast_01` (water/reflection), a particle-heavy
    scene, and the HUD/font rendering.

### Results

**2026-09-24 — Gate 2 run.** Build config: MSVC x64 `-T release` at
`6641b4ca`, deployed to the mount `D:\SourceEngine-Clean\game\` (run via
`game\run_hl2_x64.bat`: `-console -condebug -novid`).

- **6. PASS** — `save smoke64` / `load smoke64` round trip on
  `d1_trainstation_01`; `save\smoke64.sav` header =
  `4A 53 41 56 73 80 00 00` (JSAV + 0x8073, arch-stamped). Evidence:
  `game\hl2\save\smoke64.sav`, session in `game\hl2\console.log`.
- **11. PASS** — `dumpbin /headers` sweep of all 27 deployed binaries
  (`game\bin\*.dll`, `game\hl2\bin\*.dll`, `hl2_launcher.exe`):
  27/27 `8664 machine (x64)`, 0 x86, 0 failures.
- **12. PASS** — staged `save\start.sav` (JSAV, version 0x0073 unstamped)
  rejected at `SaveReadHeader` with the tailored "written by a 32-bit
  build" warning, followed by `Save file save\start.sav is not valid`.
  The intermediate `Map 'gamestate.txt' missing or invalid` line is the
  known uninitialized-`gameHeader` defect (record-only,
  `host_saverestore.cpp`). Evidence: `game\hl2\console_smoke12.log`.

Content note: 78 of the 79 mounted campaign maps are valid VBSP v20;
`d2_coast_02.bsp` is 0 bytes **in the source copies themselves**
(pre-existing content hole — the engine rejects it cleanly with
`map load failed: d2_coast_02 not found or invalid`). `d2_coast_01.bsp`,
needed for checklist 14, is valid (21.8 MB).

## 4. Phase gates (definition of done)

- **Gate 0**: this file exists; lint green in CI; build + tests green on
  both architectures; smoke checklist documented. (Reached when this
  commit lands green on `origin/main`.)
- **Gate 1**: `x360_refs`, `isx360_fn`, `xbox_include`, `fn3dnow` at 0;
  `mathlib/3dnow.*` deleted (staged plan: [phase1a.md](phase1a.md); `dx9sdk/`
  moved to Gate 3 - waf still links `dx9sdk/lib/amd64` while the D3D9
  backend lives); dead modules deleted (Phase 1b):
  `networksystem/`, `common/networksystem/`, top-level `replay/`,
  `gcsdk/`, `public/gcsdk/`, `tracker/`, `sourcevr/`, `common/python/`,
  `public/python/`, `app/`, `devtools/swigwin-1.3.34/`, the dead GC
  files (`portal_gc*`, `replayyoutubeapi.cpp`, `public/iexternaltest.h`);
  build green.
  Kept on purpose: `stub_steam/` (Steam API stub that lets the waf build
  run without Steamworks), `common/replay/` + `game/client/replay/` (live
  replay support) and `public/sourcevr/` (VR interface header).
- **Gate 2**: x64-only build; `win32_long_no_ptr` and `inline_asm` at 0;
  no C4311/C4302 warnings; save/load works.
- **Gate 3**: `d3d9_com_types` and `dx_to_gl_abstraction` at 0; `togl/`,
  `togles/`, `stdshaders/*_dx6|dx7|dx8`, `dx9sdk/` deleted; boots on DX11;
  Vulkan backend at parity.
- **Gate 4**: `grep -r '#include [<"]vgui/'` returns nothing; `gameui/`,
  `vgui2/`, `vguimatsurface/` deleted; new retained-mode UI serves main
  menu, options, loading and HUD; smoke checklist all green.
  The character class is load-bearing, not cosmetic: all 1133 vgui
  includes in the tree use the angle form `#include <vgui/...>` and the
  quoted form has **zero** occurrences, so the originally written
  `'#include "vgui/'` pattern is already satisfied at baseline while 482
  files still depend on vgui — as drafted, this gate would have passed
  without a single file being migrated. Measure it as
  `'#include [<"]vgui/'` and take the baseline count (482 files / 1133
  lines) down to nothing.
  Maintainer requirement (2026-09-25, verbatim): *"lets dont repeat
  faults that valve made, new ui must be easy-replacable. i've heard
  of UI Abstraction Layer, so its not implemented very deep like
  vgui2."* Operationally: game and engine code talks to the new UI
  only through a thin, toolkit-agnostic UI abstraction layer (views
  and state in, user actions out); no toolkit types cross that
  boundary, so the concrete implementation stays swappable without
  re-plumbing call sites. The implementation must be retained-mode
  (web-based or a modern retained-mode toolkit rendering over the new
  DX11/Vulkan back end) — no immediate-mode UI such as dear imgui.
