# Modernization Program - Phase 0: Baseline & Tooling

This directory tree records the Phase 0 baseline for the Source Engine
modernization effort (see the full roadmap in the project plan):

    Phase 0  Baseline, CI gates, smoke checklist          <-- this file
    Phase 1  Legacy/dead code removal (_X360, dx9sdk, dead modules, macros)
    Phase 2  x86 -> x64 port
    Phase 3  Graphics: remove FFP/DX6-9/togl, new RHI (DX11 + Vulkan)
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
| isx360_fn              |   910 | `IsX360()` calls - Phase 1a |
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
# 32-bit (current default - keep green until Phase 2 removes it)
./waf.bat configure -T debug --32bits
./waf.bat build

# 64-bit (must stay green; this becomes the only target in Phase 2)
./waf.bat configure -T debug
./waf.bat build
```

Linux / macOS (existing scripts, also used by CI):

```
scripts/build-ubuntu-i386.sh
scripts/build-ubuntu-amd64.sh
scripts/build-android-armv7a.sh
scripts/build-macos-amd64.sh
```

Existing test scripts (also used by CI):

```
scripts/tests-ubuntu-i386.sh
scripts/tests-ubuntu-amd64.sh
scripts/tests-macos-amd64.sh
```

Existing CI workflows: `.github/workflows/build.yml` (i386 + amd64
Windows/Linux/Android), `tests.yml`, and now `lint.yml`.

### Warning-count baseline (TODO during Phase 2 start)

Snapshot the compiler warning count so it cannot regress:

```powershell
./waf.bat build -j 8 2>&1 | Select-String -Pattern 'warning [CW]\d+' |
    Measure-Object | Select-Object -ExpandProperty Count
```

Record the number here when first measured, and ratchet it down.

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
12. Save files from the 32-bit build still load (or a migration path is
    documented).

Phase 3 additions:

13. Backend reports `D3D11` / `Vulkan` (new RHI backend name) instead of DX9/GL.
14. Screenshot comparison vs the DX9 baseline for: `d1_trainstation_01`
    (worldcraft props), `d2_coast_01` (water/reflection), a particle-heavy
    scene, and the HUD/font rendering.

## 4. Phase gates (definition of done)

- **Gate 0**: this file exists; lint green in CI; build + tests green on
  both architectures; smoke checklist documented. (Reached when this
  commit lands green on `origin/main`.)
- **Gate 1**: `x360_refs`, `isx360_fn`, `xbox_include` at 0; `dx9sdk/`,
  `common/python/2.5`, `mathlib/3dnow.*`, `networksystem/`, `replay/`,
  `gcsdk/`, `tracker/`, `stub_steam/`, `sourcevr/` deleted; build green.
- **Gate 2**: x64-only build; `win32_long_no_ptr` and `inline_asm` at 0;
  no C4311/C4302 warnings; save/load works.
- **Gate 3**: `d3d9_com_types` and `dx_to_gl_abstraction` at 0; `togl/`,
  `togles/`, `stdshaders/*_dx6|dx7|dx8` deleted; boots on DX11; Vulkan
  backend at parity.
- **Gate 4**: `grep -r '#include "vgui/'` returns nothing; `gameui/`,
  `vgui2/`, `vguimatsurface/` deleted; new retained-mode UI serves main
  menu, options, loading and HUD; smoke checklist all green.
