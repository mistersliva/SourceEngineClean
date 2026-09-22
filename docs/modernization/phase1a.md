# Phase 1a — Legacy platform code removal (X360 / PS3 / 3dnow / macro wrappers)

Part of the Phase 1 gate ([phase0.md](phase0.md) Gate 1). Scoping completed against
`d7fc33a6` (post-Phase-1b). Stage commits land only after Phase 1b's Build/Tests CI
is green.

## Inventory (scoping numbers, baseline at ratchet time)

| Category | Count | Notes |
|---|---|---|
| `_X360` condition lines | 1,531 lead-shape + 177 mixed | `#ifdef` 295 · `#ifndef` 244 · `#if !defined` 382 · `#if defined` 610 |
| `_X360` non-condition (comments/code) | ~278 | manual sweep |
| `IsX360()` call sites | 911 | `if()` 456 · `if(!…)` 88 · `&&` 135 · `\|\|` 78 · ternary 53 · `return` 4 |
| `#include "xbox/…"` | 165 (lint) | most guarded by `_X360`/`!_X360`; a few unguarded (`WinApp.cpp`) |
| `$X360` VPC condition lines | 164 | `projects.vgc`, `groups.vgc`, per-project `.vpc` |
| `mathlib/3dnow.*` refs | 125 | consumers below |
| `_PS3` (bonus, confirmed) | 742 | `_PS3` only defined under `#ifdef SN_TARGET_PS3` — never true for waf/CI |
| `IsPS3()` (bonus, confirmed) | 19 | same shape as `IsX360()` |

Distribution (x360_refs): materialsystem 474 · engine 245 · public 245 · external/vpc 232 ·
game 129 · tier0 118 · vgui2 84 · gameui 67 · utils 50 · studiorender 36 · rest ≤ 32 each.
Top files: `shaderapidx8.cpp` 75, `dynamicvb.h` 28, `cmaterialsystem.cpp` 28,
`shaderdevicedx8.cpp` 26, `dynamicib.h` 26, `r_studiodraw.cpp` 25.

## Maintainer decisions (confirmed)

1. **`dx9sdk/` moved from Gate 1 to Gate 3.** waf links `dx9sdk/lib/amd64` today and
   `d3dx9.lib` exists only there (not in the Windows SDK); the SDK dies with the D3D9
   backend in Phase 3.
2. **PS3 unwrap included** in Stages 3–4 (same shapes; not gate-counted — tracked here).
3. **`external/vpc/**` included** in unwrap scope (232 refs; required for a literal
   whole-tree `x360_refs = 0`). The vpc tool is not built by CI → verify by building it
   locally after Stage 3.

## Kept on purpose (live code, xbox *name* only)

- `engine/xboxsystem.cpp`, `public/ixboxsystem.h` (or wherever it lives), `engine/Session.h`
  — in active waf source lists / included unguarded; matchmaking session interface.
- `gameui/*_Xbox.cpp` — compiled by `gameui/wscript`; GameUI dies wholesale in Phase 4.
- `tier0/cpu.cpp` 3dnow *detection* stays until Stage 2 pairs it with `MathLib_*` removal.

## Out of scope (discovered during scoping, deferred)

- **`common/quicktime_win32/` — 345 files.** `$QUICKTIME_WIN32` is never defined by waf;
  Mac-only legacy video capture. Candidate for a follow-up pass (Phase 1c).
- `memdbgon.h` alloc wrappers and POSIX `stricmp` compat wrappers = infrastructure, kept.
- Immediate-mode/dead networking already handled by Phase 1b.

## Stages

Each stage: patch → `powershell -ExecutionPolicy Bypass -File scripts\lint-legacy.ps1 -UpdateBaseline`
→ full local `.\waf.bat configure -T debug && .\waf.bat build` → commit. Push as soon as
Phase 1b CI reports green (each stage then gets its own CI run).

1. **Dead trees + VPC registry.** Delete `utils/xbox/` (136 files), `common/xbox/`,
   `tier0/xbox/`, per-module `xbox/` dirs, `vtf/vtf_x360.cpp`, `vtf/convert_x360.cpp`,
   `tier0/vcrmode_xbox.cpp`, 8× `vpc_scripts/source_*_x360_*.vgc` + x360 definitions;
   strip 164 `$X360` conditions; remove `$Group "xbox_utils"` + x360 `$Project` entries;
   remove guarded `$Include …x360…` lines; delete inert `[$X360]` markers in `engine/wscript`.
   Unguarded/PC-side `#include "xbox/…"` sites removed here (before unwrap can
   unconditionalise them), with symbol-usage verification for `xboxstubs.h` consumers.
2. **3dnow.** Delete `mathlib/3dnow.{h,cpp}`; fix `engine/initmathlib.cpp` (`r_3dnow`
   concommand + `MathLib_Init` flag), `engine/host.cpp` feature string,
   `mathlib/mathlib_base.cpp`, `mathlib.vpc`, `tier0/cpu.cpp` feature flags
   (`m_b3DNow` field + both producer/consumer sides together).
3. **Preprocessor unwrap (scripted).** Python `#if`-stack tool over all `.c/.cc/.cpp/.h/.inl`
   incl. `external/vpc`:
   - `#ifdef _X360` / `#if defined(_X360)` (+`_PS3`) → drop block (keep `#else` arm);
   - `#ifndef _X360` / `#if !defined(_X360)` → unwrap body (drop `#else` arm);
   - mixed: `!X && REST` → `REST` · `X && REST` → drop · `X || REST` → `REST` · `!X || REST` → always-true;
   - `X360 || PS3` → fully dead;
   - then manual sweep for ~278 stragglers (comments, `#define`-adjacent, macro bodies).
   Verification: full local build + `git grep` counts → condition lines at 0.
4. **`IsX360()` / `IsPS3()` elimination.** Brace-matching script for the 544 `if()` forms
   (fold else-arms), logical simplification for the 213 `&&`/`||`, manual ternaries (53)
   and `return` (4); delete macro definitions in `public/tier0/platform.h` (lines ~121/142/155)
   + `IsPlatformX360` plumbing; hunt `git grep` to 0.
5. **Macro wrappers.** Local file-scope `#define min/max` (~8 files: voice_record_openal,
   voice_codec_frame, buypreset_listbox, career_box, TextEntryBox, entcount, …) →
   `std::min/max` or inline ternary. Nothing else qualifies (see *Out of scope*).

## Stage 1 execution notes (completed, uncommitted)

Result: lint `xbox_include` 165 → **0**; `x360_refs` 1911 → 1871; `isx360_fn` 910 → 901.
Baseline ratcheted (`scripts/lint-baseline.json`). Scripted via a byte-safe
transformation script with verify greps (all 13 checks clean).

- **Deletions:** 554 files — `utils/xbox/` (136 files), `common/xbox/`, `tier0/xbox/`,
  20 per-module `xbox/` dirs, `fxctmp9_360`/`vshtmp9_360`/`UPDB_X360`, 9×
  `vpc_scripts/source_*_360_*`, `definitions/xbox360*.def`, `make360def.pl`,
  `projectgenerator_xbox360*` (6 files), `dx_proxy_dx9_v00_x360.vpc`, single files
  (`vtf_x360`, `convert_x360`, `vcrmode_xbox`, `pmc360`, `textureheap.cpp`,
  `Win32Font_x360.cpp`, `rendertargetblit_x360.cpp`, `basepresence_xbox.cpp`,
  `xbox_codeline_defines.h` ×2) plus orphan `engine/audio/snd_dev_xaudio.cpp` +
  `snd_wave_mixer_xma.cpp` (in no build list).
- **VPC condition strip:** 62 line-kills + 24 block-kills, 80 token strips
  (`[$WINDOWS||$X360]` → `[$WINDOWS]`, `[!$X360 && !$OSXALL]` → `[!$OSXALL]`, …),
  18 path-kills (`utils\xbox`, `dx_proxy_dx9_v00_x360`); 0 unhandled shapes.
- **groups/projects.vgc:** `$Group "xbox_utils"` + 26 member lines of 29 emptied
  `$Project` wrappers removed.
- **vpc tool surgery:** `main.cpp` banner / VS2010 condition / RETAIL-console condition /
  PLATSUBDIR X360 macro branch / `CheckForInstalledXDK` / generator-selection branch
  removed; `conditionals.cpp` X360 platform registration removed; `vpc.h` includes+decl;
  `vpc.vcxproj` (8 lines), `.filters` (8), `dx_proxy.sln` (project + GUID config lines).
  **Kept:** `KEYWORD_XBOXIMAGE` / `$Xbox360ImageConversion` parser machinery (no remaining
  users, but it lives in kept files) and VPC PS3 support.
- **`dx_proxy.cpp`:** entire `DX9_V00_X360` configuration removed (55 lines: config
  block + comment header, version strings, inline-arg block, or-condition, `#error` text).
- **Include sweep:** all `#include "…xbox/…"` lines removed; `xbox_codeline_defines.h`
  blocks spliced from both `basetypes.h` copies; `soundflags.h` `IN_XBOX_CODELINE`
  unwrap (13-bit branch kept).
- **Stale entries fixed:** X360 publish comments (`tier0.vpc`, `vstdlib.vpc`),
  `vtf.vpc` `convert_x360.cpp`, `tier0.vpc` `xbox_codeline_defines.h`, `engine.vpc`
  X360 audio lines, `x360xdk` include dirs (`bitmap.vpc`, `diffmemstats.vpc`).

### Build fix (first local build failed → PC-live shim restored)

The first `waf build` after the sweep failed in `common/netmessages.h`
(`XUSER_CONTEXT` undeclared, …): the include sweep had removed **PC-active**
includes of `common/xbox/xboxstubs.h`. That header is the PC-side shim —
`XNADDR`/`XNKEY`/`XNKID`/`XUID`/`XUSER_*`/`XSESSION_*` types plus `FORCEINLINE`
`XBX_*` stub implementations (how all unguarded `XBX_GetStorageDeviceId()` callers
linked on PC). Fix, verified by guard-stack + symbol-usage scans over the whole
tree:

- **Header classification (all 6 `common/xbox/` headers):** `xboxstubs.h` = PC-live
  (restored as `common/xboxstubs.h` — de-xboxed path, byte-identical content,
  same `XBOXSTUBS_H` guard). `xbox_win32stubs.h`, `xbox_console.h`,
  `xbox_launch.h`, `xbox_core.h`, `xbox_vxconsole.h` = X360-only content; every
  include site is X360/`PLATFORM_X360`-guarded and no surviving PC code uses
  their symbols outside what `xboxstubs.h` provides → stay deleted.
- **21 PC-active include sites restored** with rewritten paths
  (`"xbox/xboxstubs.h"` → `"xboxstubs.h"`, `"../common/xbox/xboxstubs.h"` →
  `"../common/xboxstubs.h"`); the original guard wrappers are untouched.
  Resolution is preserved: `-Icommon` that made `xbox/xboxstubs.h` resolve makes
  `xboxstubs.h` resolve identically; the `../common/…` form resolves through
  depth-1 include dirs exactly as before.
- **X360-only sites stay removed**; `WinApp.cpp`'s unconditional include of
  `xbox_console.h` (zero symbol usage — vestigial) stays removed.
- Lint `xbox_include` stays **0** (pattern needs a literal `xbox/`); the Stage-1
  verify `utils/common xbox paths` was tightened to
  `utils[/\\]xbox\b|common[/\\]xbox\b` so de-xboxed paths don't false-positive.
- Classifier note: `PLATFORM_X360`-guarded sites (`tier0/stacktools.*`,
  `external/vpc/public/tier0/platform.h`) count as X360-only.

### Late discoveries (tracked, not Stage-1 blockers)

- **`_XBOX` = 104 refs** (distinct from `_X360`) → added to Stage 3 unwrap list.
- **86 pre-existing stale `$File` entries** in 13 `.vpc` files — upstream nillerusr-era
  rot (waf is ground truth; the vpc toolchain is unmaintained). Only the X360-caused
  ones were fixed here; consider a future "vpc retirement" item.
- **`NO_X360_XDK`** still defined at `wscript:250` + `bitmap.vpc:19` and guards
  `bitmap/ImageByteSwap.cpp`'s `x360xdk` includes — all three die together in Stage 3.
- **`.pl`/`.bat` x360 modes deferred:** `fxc_prep.pl`, `vsh_prep.pl`, `psh_prep.pl`,
  `copyshaders.pl`, `buildshaders.bat`, `clean*.bat`; `fixcopyrights.py` `bink_x360`
  and `x360xdk` skip-list entries.
- **`devtools/bin/` prebuilt x360-era binaries remain** (fontmaker/makewav/xbspinfo
  .exe/.pdb, `vpc.pdb` embed `utils\xbox` strings) — not deleted.
- **Guarded headers kept:** `textureheap.h`, `engine/audio/snd_dev_xaudio.h`,
  `snd_wave_mixer_xma.h` (the latter two included by `audio_pch.h`) — `_X360` content
  dies in Stage 3.
- **VPC `console`/`licensee_console` groups kept** (only `xbox_utils` removed).
- **Whitelisted residue:** `vprof.h` ×2 (`pmc360.h` include inside dead `_X360` guard →
  Stage 3), `ImageByteSwap.cpp` (`NO_X360_XDK` guard → Stage 3).

## Gate 1 completion criteria (updated)

`x360_refs = 0`, `isx360_fn = 0`, `xbox_include = 0` in lint; `mathlib/3dnow.*` deleted;
dead modules deleted (done — Phase 1b); full local build green; CI green on the stage
commits. ~~`dx9sdk/` deleted~~ → moved to **Gate 3**.
