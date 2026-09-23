# Phase 1a — Legacy platform code removal (X360 / PS3 / 3dnow / macro wrappers)

Part of the Phase 1 gate ([phase0.md](phase0.md) Gate 1). Scoping completed against
`d7fc33a6` (post-Phase-1b). Stage commits land only after Phase 1b's Build/Tests CI
is green.

## Inventory (scoping numbers, baseline at ratchet time)

| Category | Count | Notes |
|---|---|---|
| `_X360` condition lines | 1,531 lead-shape + 177 mixed | `#ifdef` 295 · `#ifndef` 244 · `#if !defined` 382 · `#if defined` 610 |
| `_X360` non-condition (comments/code) | ~278 | manual sweep |
| `IsX360` call sites | 911 | `if()` 456 · `if(!…)` 88 · `&&` 135 · `\|\|` 78 · ternary 53 · `return` 4 |
| `#include "xbox/…"` | 165 (lint) | most guarded by `_X360`/`!_X360`; a few unguarded (`WinApp.cpp`) |
| `$X360` VPC condition lines | 164 | `projects.vgc`, `groups.vgc`, per-project `.vpc` |
| `mathlib/3dnow.*` refs | 125 | consumers below |
| `_PS3` (bonus, confirmed) | 742 | `_PS3` only defined under `#ifdef SN_TARGET_PS3` — never true for waf/CI |
| `IsPS3` (bonus, confirmed) | 19 | same shape as `IsX360` |

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
4. **`IsX360` / `IsPS3` elimination.** Brace-matching script for the 544 `if()` forms
   (fold else-arms), logical simplification for the 213 `&&`/`||`, manual ternaries (53)
   and `return` (4); delete macro definitions in `public/tier0/platform.h` (lines ~121/142/155)
   + `IsPlatformX360` plumbing; hunt `git grep` to 0.
5. **Macro wrappers.** Local file-scope `#define min/max` (~8 files: voice_record_openal,
   voice_codec_frame, buypreset_listbox, career_box, TextEntryBox, entcount, …) →
   `std::min/max` or inline ternary. Nothing else qualifies (see *Out of scope*).

## Stage 1 execution notes (completed, commit `8b9421c2`)

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

## Stage 2 execution notes (3dnow)

Result: 3 files deleted, ~60 lines of symbols removed across 25 files, 26
`MathLib_Init` call sites rewritten; scripted via
`stage2_3dnow.py` (byte-safe, idempotent — verified green on re-run). Lint gained
a new ratchet id **`fn3dnow`** (baseline **0**), wired into the Gate 1 criteria.

- **Deleted:** `mathlib/3dnow.cpp`, `mathlib/3dnow.h`, `public/mathlib/amd3dx.h`
  (only includers were each other + `mathlib_base.cpp`, verified before deletion).
- **`MathLib_Init`:** the `bAllow3DNow` parameter (pos 5) is gone from the
  signature in `public/mathlib/mathlib.h` (+ external copy) and
  `mathlib/mathlib_base.cpp`; the 5th argument was dropped from all 26 call
  sites (8-arg calls, plus the legacy 7-arg `true,…` shapes in
  `matsysapp.cpp`/`d3dapp.cpp`). `s_bAllow3DNow` + the multi-line call in
  `engine/initmathlib.cpp` and the `r_3dnow` concommand (20 lines) were removed;
  `sse2`/`sse`/`mmx` dispatch and concommands are untouched.
- **`mathlib_base.cpp`:** the 3dnow selection block (20 lines: `#if
  bAllow3DNow && pi.m_b3DNow` → `_3DNow_*` function-pointer assignments →
  `else` arm), `s_b3DNowEnabled`, the `MathLib_3DNowEnabled()` body, and the
  `amd3dx.h`/`3dnow.h` includes (with the `#ifndef OSX` wrapper) are gone;
  the `#if !defined(_X360)` → `sse.h` → `#endif` structure stays for Stage 3.
- **CPU plumbing:** `Check3DNowTechnology` (real impls in `tier0/cpu.cpp` and
  external copy + stubs/impls in both `processor_detect` variants) deleted; the
  `pi.m_b3DNow = …` assignments and the `m_b3DNow : 1` bitfield (both
  `platform.h` copies) deleted; header decl dropped.
- **`engine/host.cpp`:** the `(3DNow)` feature-string block (6 lines) deleted.
- **Build refs:** `mathlib.vpc` (3dnow.cpp/3dnow.h/amd3dx `$File` lines),
  `mathlib/wscript`, `amd3dx.h` `$File` lines in 19 other `.vpc` files, and
  the `fixcopyrights.py` skip entry.
- **Kept on purpose:** `K8PerformanceCounters.h` `CombinedMMX_3DNow` (historical
  symbol name, not a 3DNow path), `cardstats.cpp` `"3DNOW"` card string,
  `linux/make_check/**` stale `.dsp.check` caches — none match `fn3dnow`.

## Stage 3 execution notes (`_X360`/`_PS3`/`_XBOX` conditionals)

Tooling: comment-aware `#if`-tree unwrapper (`stage3_unwrap.py`) + residue
sweep (`stage3_sweep.py`), both external to the repo. Two intermediate runs
were discarded after self-testing exposed real bugs; the committed run is
gated by a 10-case synthetic self-test that runs before any tree edit:

- **Scanner bug:** multi-line comment state bailed on the first line lacking
  `*/`, so commented `#endif*/` markers counted as real (3 stack aborts).
- **Kind-clobber bug:** `build_groups` overwrote arm0's kind to `"if"`, so
  every `#ifndef TARGET` evaluated as bare-ident `#if TARGET` (FALSE) —
  inverted arm selection: dead else-arms kept, live PC bodies deleted
  (`platform.h` lost `IsPC() true`/`PLATFORM_WINDOWS_PC`). `#ifdef` groups had
  been correct only by accident (bare-ident FALSE ≡ `defined()` FALSE).
- **Elif-promotion bug (caught by the self-test before touching the tree):**
  REDUCE with a promoted `#elif` head deleted arm0's directive (which *is*
  `g.if_line`) and also rewrote it; `del_set` wins in materialize → the `#if`
  line vanished, leaving bare code + dangling `#else`. Fixed by skipping
  `g.if_line` in that loop.
- **Continuation-loss bug (found by the CI gate after push, missed by the
  self-test):** REDUCE on a multi-line `#if cond &&\` directive wrote the
  joined reduced condition to the head line *without* its trailing `\` but
  left the continuation line behind as orphaned code (`defined(...)` at code
  position → `error C3861`; windows-i386 only, because the surviving
  condition is gated on `!defined(_M_X64)`). 6 sites repaired by deleting
  the orphan lines (`tier0/stacktools.cpp` and
  `external/vpc/tier0/stacktools.cpp` ×3 — the joined head line already
  carried the correctly reduced condition). A tree+diff audit for lost
  continuations (`audit_orphans.py`) reports no other site in the tree.

Truth model: `_X360`/`_PS3`/`_XBOX` never defined; `NO_X360_XDK` TRUE at
resolution time (its `wscript` + `bitmap/bitmap.vpc` defines removed by the
tool). Never-defined also grep-verified (no code/VPC/waf defines anywhere):
`PLATFORM_X360` (both `#define`s sit only inside the dead `_X360` else-arms
of the `platform.h` copies), `REVERSE_DEPTH_ON_X360`, `X360_USE_SIMD_LIGHTMAP`.

Result (tool): **591 files changed; 2,318 groups resolved** — 491 reduced
(UNKNOWN folded), 1,064 unwrapped (TRUE arm kept bare), 763 deleted
(all-FALSE); 0 aborts, 0 kept-unparseable, 0 targeted directive lines;
idempotent re-run changes 0 files (exit 0). Sweep: 25 stale directive comments
trimmed, 8 commented-out platform directives deleted, 66 exact replacements —
renames (`MAIN_MENU_INDENT_CONSOLE`, `MD_OPTION_CHANGE_FROM_CONSOLE_DASHBOARD`,
`INPUT_TYPE_GAMEPAD`, `VTF_360_MAJOR/MINOR_VERSION`,
`MAX_360_RSRC_DICTIONARY_ENTRIES`, `BYTES/PAGESIZE_PHYS_SBH`,
`X360APPCHOOSER` → `MOVIEAPPCHOOSER` across `.cpp`/`.fxc`/checked-in `.inc`),
block deletions (`vertexshaderdx8.cpp` `"_X360"` shader macro +
`x360DefineString` block, `d3dxfxc.cpp` dead `bIsX360` macro scan,
`captioncompiler.cpp` commented `UpdateOrCreateCaptionFile_X360` block,
`"RenderTargetBlit_X360"` string), and always-true conjunct drops
(`BUILD_CURL`, both `stacktools.cpp` macro bodies).

Also settled from Stage-1/2 whitelists: `vprof.h` `pmc360.h` guard,
`ImageByteSwap.cpp` `NO_X360_XDK` arm, `mathlib_base.cpp`
`#if !defined(_X360)` → `sse.h` unwrap, `snd_dev_xaudio.h` dead content.

Lint baseline: `x360_refs` **1870 → 0**; every other id only moved down
(`isx360_fn` 901→886, `inline_asm` 136→127, `d3d9_com_types` 936→924,
`suspicious_ptr_cast` 54→45, `dx_to_gl_abstraction` 216→215) — those lines
lived in deleted arms.

Known residue (not lint-gated): `_PS3` 160 lines / `_XBOX` 40 lines of
comments & identifiers (e.g. `INLINE_ON_PS3`); `REVERSE_DEPTH_ON_X360` in
`.fxc` shader sources only (never defined → false path, same behavior as the
unwrapped C++ side); `CX360SmallBlockPool` / `USE_PHYSICAL_SMALL_BLOCK_HEAP`
(no `_X360` substring; the whole block is dead — later deletion candidate);
`.pl`/`.bat` x360 modes and docs mentions (Stage-1 deferrals).

## Stage 4 execution notes (runtime platform-check folds)

Tool: a deterministic folding script gated by a **20-case exact-output
self-test** that runs before every tree pass. Layered passes, in order:

1. **Statement fold** — three-valued evaluation (true/false/unknown) of `if`
   conditions over a comment/string-blanked skeleton: false headers deleted
   with their then-arm (else-arm promoted where present), true `else if`
   arms unwrapped, unknown conditions rewritten with the atom folded out.
2. **Ternary fold** — `atom ? a : b` with a statically known atom picks the
   live branch (the chosen text is sliced from the original source, never
   the skeleton).
3. **Value-expression fold** — assignment/initializer extents containing the
   atom evaluate to `false`/`true`/reduced text.
4. **Atom → `false` fallback** — any remaining code-position call replaced
   textually (the macro is compile-time `false` on PC, so this is always
   semantics-preserving); comment/string positions are skipped via the
   skeleton mask.
5. **Define deletion** — the seven predicate macro definition lines (five in
   `public/tier0/platform.h`, two in the VPC copy) removed only after the
   tree reports zero code-position call sites.

Safety model: structural oddities (unmatched `#endif` inside a statement
span, conditions split across `#if/#else` branches,
assignment-inside-comparison conds, statements inside MSVC `__asm` comment
regions) are **skipped per candidate with a recorded reason** instead of
aborting the whole file — the atom still folds to `false` via pass 4, so
output stays correct while preprocessor structure is preserved; a final
batch balance-check is the only file-level abort. Two length-preserving
skeletons (statement + atom) drive all offset math; raw bytes round-trip
(utf-8 + surrogateescape) so CRLF and CP1252 files are untouched.

Bug found by the compiler (and fixed): the first version sourced
unknown-condition rewrite text from the skeleton, whose masking blanks
string literals — `FindParm( "-allowdebug" )` came out as
`FindParm(               )`. The lexer now tokenizes the skeleton for
structure but carries original-source leaf text (gap-attached, so literals
and comments between tokens survive). Four affected files (`interface.cpp`,
`host_saverestore.cpp`, `mouseoverpanelbutton.h`, `BonusMapsDatabase.cpp`)
were reverted and re-folded; two independent diff detectors (interior
3+-space / empty-paren signature, per-hunk quote-count loss) then scanned
every added line — remaining hits are correct ternary folds plus one
pre-existing spacing quirk (`NVFMT_INTZ   `).

Manual comment sweep: 13 source lines where a predicate call survived only
inside comments — six were wholesale deletions of Valve's own
`/* commented out ... remove entirely when new implementation settles */`
blocks across the four `threadtools` copies plus `CColorCorrection.cpp` and
`cmatlightmaps.cpp` (617 lines of dead text), the rest single `//` lines
(11 lines) and stale doc mentions (doc prose now names the symbols without
call parens so the repo-wide grep gate holds).

Result: **251 files changed, +864/−5,760**; zero call-form matches for the
three predicates repo-wide (code, comments, docs); idempotent re-run changes
0 files with 0 residue and 0 aborts; lint `isx360_fn` **886 → 0** (baseline
ratcheted); full local build green (2,214/2,214 tasks).

## Stage 5 execution notes (local min/max macro wrappers)

Scope rule (applied repo-wide via case-sensitive `git grep`, complete for
tracked code): **file-scope hand-rolled `min`/`Max` wrappers in first-party
code are in; the uppercase `MIN`/`MAX` convention and infrastructure are out.**

In scope = **16 defines across 15 files**: the 15 lowercase `#define min`/
`#define max` wrappers (voice_record_openal, voice_codec_frame, the seven
vgui_controls files, TextEntryBox, career_box, buypreset_listbox, entcount)
plus `public/XZip.cpp`'s mixed-case `Max`. Eleven wrappers had call sites;
five were dead on arrival (ListPanel's `max`, RichText's `max`, ProgressBox,
career_box, buypreset — each guarded `#ifndef` had made the local define a
silent no-op wherever `platform.h` already supplied one).

Kept on purpose: **uppercase `MIN`/`MAX`** — `basetypes.h` (+ the vpc copy)
is public API consumed by **452 files**, `texpow2.h`/`jpeglib` follow the
same convention; **`valve_minmax_on.h`** — deliberate tier0 infrastructure
(included from `platform.h`, paired with `valve_minmax_off.h`); **vendored**
`utils/vmpi_private/mysql/include/my_global.h`; the **`ivp` submodule**
(`geompack.hxx`, invisible to `git grep` anyway); and ListPanel's **`clamp`**
wrapper — its `min`/`max` are macro *parameter* names (substituted before
rescan, fully independent of the wrappers), it has a live call site, and it
is not `min`/`max` per the stage definition.

Tool: deterministic script gated by a **19-case exact-output self-test**
that runs before every tree pass. Pass A is line-based (delete a matching
`#ifndef`/`#define`/`#endif` triplet or a plain `#define`, plus two
explicitly-listed dead comment lines that document the removed macro).
Pass B rewrites call sites to a fixed point through a linear state scanner
that skips comments, string/char literals and preprocessor directive lines,
rejects qualified calls (`::`/`.`/`.`-style), parses balanced parens with
depth-0 comma splitting, and emits each macro's **exact expansion** —
`(((A) < (B)) ? (A) : (B))` (or `>`/`>=` as written in that macro's body;
XZip's `Max` really uses `>=`). The pre-run self-test caught five real
defects before the tree was touched: guard-triplet detection reading the
wrong line, the search phase not being comment-aware, the `Max` operator
map, an inverted exit code, and a mismatched fixture.

Why the ternary and not `std::min`/`std::max`: the emitted text is
token-identical to today's preprocessing (same operand parenthesization,
same double-evaluation, same mixed-type usual arithmetic conversions).
`std::min`/`std::max` would *not compile* at the QueryBox/MessageBox sites
(`max(oldWide, btnWide + 10 * scale)` mixes `int` and `float`) and would
silently change evaluation count everywhere — out of bounds for a
behavior-preserving stage. RichText's two `min` call sites looked external
and were: they resolve through `platform.h` → `valve_minmax_on.h` (a macro,
not `std::min`), so they were converted to the identical expansion too.

Result: **15 files changed, +23/−67**; 16 defines deleted, 25 call
expressions converted (23 source lines — the nested voice_codec call
duplicates its inner expression exactly as the macro did), 2 dead comment
lines removed; call-form grep over the 15 files = **0**; residual wrapper
grep = **6** (the documented `valve_minmax_on.h` + `my_global.h` residue);
new lint id `local_minmax_macro` ratcheted **22 → 6**; idempotent re-run
changes 0 files; full local build green (2,214/2,214 tasks).

## Gate 1 completion criteria (updated)

`x360_refs = 0`, `isx360_fn = 0`, `xbox_include = 0`, `fn3dnow = 0` in lint;
`mathlib/3dnow.*` deleted;
dead modules deleted (done — Phase 1b); full local build green; CI green on the stage
commits. ~~`dx9sdk/` deleted~~ → moved to **Gate 3**.
