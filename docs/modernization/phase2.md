# Phase 2 — x86 → x64 port (64-bit-only build + pointer-width hygiene)

Part of the Phase 2 gate ([phase0.md](phase0.md) Gate 2). Scoping completed
against `7667fe09` (the commit that closed Gate 1 / Phase 1a).

## Key scoping finding: x64 already builds — Phase 2 *removes* x86

The engine has been building x64 all along: `wscript` sets
`conf.env.MSVC_TARGETS = ['x64']` by default — the `-4` / `--32bits` option
is what flips it to `['x86']` — and the `engine.dll` produced at scoping
time carries PE machine `0x8664`. The local full build, the amd64 CI jobs
(Windows / Linux / macOS) and the test suites are green on x64 today.

So this phase is not a port *to* 64-bit. It is:

1. **delete the 32-bit targets** — the waf option, the i386 CI jobs and
   scripts, and the obsolete Android armv7 job — so 64-bit is the *only*
   build, and
2. **burn down the x86-era residue** the lint ids measure: i386 inline
   assembly surviving in never-true guards, truncating Win32 `Long` APIs,
   and pointer → 32-bit-int casts. The authoritative truncation gate is
   the compiler (MSVC C4311/C4302), fed by the warning-count baseline that
   `phase0.md` §2 left as a TODO for phase start.

(`phase0.md` §2 still calls 32-bit the "current default" — stale; the
wscript default is x64. Stage 2 corrects those doc lines.)

## Gate 2 criteria (phase0.md §4)

- x64-only build;
- `win32_long_no_ptr` and `inline_asm` at 0;
- no C4311/C4302 warnings;
- save/load works.

## Inventory (scoping at `7667fe09`, lint baselines)

| Category | Count | Distribution / notes |
|---|---|---|
| `inline_asm` | **127** hits / 30 files | `external/vpc` 43 (its vendored tier0 copies), `public/` 26, `utils/` 25 (lzma `CpuArch.c` 21), `tier0/` 12, mathlib/sse 10, `vstdlib/coroutine` 5, engine/snd_mix 4, misc 6 |
| `win32_long_no_ptr` | **63** hits / 22 files | utils 43 (hlfaceposer 24, scenemanager 14), engine 8 (sys_getmodes 5), hammer 7, vgui2/src 3, devtools 2 |
| `suspicious_ptr_cast` | **45** hits / 15 files | vgui2/InputWin32 16, hammer 22, public 2 (string_t, utldelegateimpl), tier0+vpc stacktools 2, vrad 2, gamemovement 1 |
| `_M_IX86` guard matches | 25 | MSVC arch gating around asm |
| `__i386__` guard matches | 66 | GCC/Clang arch gating |
| `_WIN64` / `PLATFORM_64BITS` | 79 / 69 | pre-existing 64-bit conditionals — mostly *kept* |
| `Get/SetClassLong`, `Get/SetWindowWord` | 0 | checked — no other truncating Long/Word-family API is in use |

Warning baseline (total `warning C/W` count, per-code histogram, and the
C4311/C4302 subtotal Stage 5 must drive to 0) is measured by Stage 1 on a
clean full rebuild and recorded in `phase0.md` §2.

## Maintainer decisions (confirmed)

1. **Android CI is dropped, not migrated.** `build-android-armv7a` targets
   32-bit `armeabi-v7a` on android-ndk **r10e / gcc 4.9 (2015)** and passes
   `--togles` — a stack Gate 3 deletes wholesale. It fails Gate 2 for being
   32-bit and would fail Gate 3 for building at all. The job and
   `scripts/build-android-armv7a.sh` are removed in Stage 2. Android may
   return later as arm64 + a modern NDK — a separate effort, post-Gate 3.
   (Confirmed by the maintainer at scoping time.)
2. **32-bit saves are documented incompatible, not emulated.** Stage 6
   audits the datadesc/save format, gates *same-arch* x64 save/load, and
   documents that saves written by x86 builds require the x86 build — the
   smoke checklist's "or a migration path is documented" branch. (Confirmed
   by the maintainer at scoping time.)
3. **"x64-only" means "64-bit-only" (scoping call, evidence-backed).**
   Every *32-bit* target is removed; 64-bit non-x86 targets are untouched.
   This is forced by the scripts themselves: `build-macos-amd64.sh` and
   `build-ubuntu-amd64.sh` pass no arch flag, so on `macos-latest`
   (arm64 runners) the macOS jobs already build **native arm64** — a
   literal "amd64-only" reading would demand deleting them, which is
   plainly not the gate's intent. waf's arm/aarch64 flag handling stays as
   infrastructure; no linux-arm CI exists to disband.
4. **VPC / Visual Studio project inputs are not the build.** Gate 2 is
   judged on waf + CI; dormant `Win32` configurations inside `.vpc` inputs
   are IDE-generation artifacts, left alone (opportunistically cleanable).
   Hammer / hlfaceposer / scenemanager *sources* are still fixed — the lint
   ids count the whole tree regardless of what waf compiles.

## Stages

Same workflow as Phase 1a: patch →
`powershell -ExecutionPolicy Bypass -File scripts\lint-legacy.ps1 -UpdateBaseline`
→ full local `.\waf.bat build -j 8` → commit → push → gate on fresh CI.

1. **Warning baseline (the `phase0.md` §2 TODO).** Clean full x64 rebuild;
   record total `warning C/W` count, the per-code histogram's headline
   numbers, and the C4311/C4302 subtotal in `phase0.md` §2. This is the
   phase's second ratchet — lint regexes cannot see implicit truncation,
   the warning stream can.
2. **Remove the 32-bit targets.** In `wscript`: the `-4/--32bits` option,
   the `TARGET32` / `BIT32_MANDATORY` plumbing, the
   `MSVC_TARGETS = ['x86']` branch (keep `['x64']`, fix the stale
   "x86 target" comment), and x86-only flag arms that become unreachable.
   In CI: drop `build-linux-i386`, `build-windows-i386`,
   `build-dedicated-linux-i386`, `build-android-armv7a` (decision 1), and
   `build-dedicated-windows-i386` — **confirmed an accidental duplicate**
   of its amd64 twin: both configure `-T debug -d` with no arch flag, so
   both build the default x64 target and the "i386" name is simply wrong;
   plus `tests-linux-i386` and `tests-windows-i386`. Scripts: delete `build-ubuntu-i386.sh`,
   `tests-ubuntu-i386.sh`, `build-android-armv7a.sh`; de-i386
   `deploy.sh`'s package bootstrap. Docs: `phase0.md` §2's 32-bit command
   block and the "current default" correction. waf's `--android`/xcompile
   tooling stays (infrastructure; no 32-bit CI target uses it anymore).
   Gate: CI green with 64-bit jobs only.
3. **`inline_asm` → 0 (127 / 30 files).** Per file: i386-only blocks
   behind `_M_IX86` / `__i386__` / `defined(_MSC_VER) && defined(_M_IX86)`
   and friends are dead in a 64-bit-only world — delete the block, unwrap
   the now-constant guard. GNU-syntax `__asm` that still compiles on x64
   (lua / lzma cpuid-style) counts toward the id anyway → replace with C or
   intrinsics (`__cpuid`, `__rdtsc`, …). `public/` headers and their
   `external/vpc/` vendored twins get identical treatment (vpc is not on CI
   — verify it locally, as in Phase 1a). For files waf does not compile,
   verification is guard-reading + lint, not compilation. Gate:
   `inline_asm = 0` + full x64 build green.
4. **`win32_long_no_ptr` → 0 (63 / 22 files).**
   `GetWindowLong[A/W]` → `GetWindowLongPtr[A/W]`,
   `SetWindowLong[A/W]` → `SetWindowLongPtr[A/W]`, and pointer-valued
   index constants (`GWL_WNDPROC`, `GWL_USERDATA`, `GWL_HINSTANCE`,
   `GWL_HWNDPARENT`) → `GWLP_*`. Value-sized indices (`GWL_STYLE`,
   `GWL_EXSTYLE`, `GWL_ID`) keep their names — the `Ptr` accessors accept
   them and widen harmlessly. Mechanical per-file pass; waf-built files
   (engine, vgui2/src) verified by compilation, tools by review + lint.
   Gate: `win32_long_no_ptr = 0`.
5. **Pointer-truncation sweep → zero C4311/C4302.** Fix every hit in the
   Stage 1 warning log and every honest `suspicious_ptr_cast`
   (InputWin32's 16, hammer's 22, stacktools, string_t, …): address-sized
   values become `intptr_t` / `uintptr_t` / `DWORD_PTR` / `uptr`; plain
   `int` survives only where the value is genuinely address-independent.
   **Worklist from Stage 1's log: the 92 C4311 split 68 / 24** —
   **68 are inside the pinned `ivp` submodule** (physics object pointers
   cast to `long`; fixing them needs an ivp fork + submodule URL repoint,
   or vendoring ivp into the superproject — maintainer decision at Stage 5
   start) and **24 are first-party**
   (`vgui2/src/InputWin32.cpp` 19, `gameui/Sys_Utils.cpp` 2, one each in
   `voice_mixer_controls.cpp`, `baseentity.cpp`,
   `vguimatsurface/Input.cpp`). Also fix
   **`public/tier1/utlmemory.h:440`** — one `reinterpret_cast<unsigned
   int>` producing 27,626 of the 27,699 C4312s and 99.4% of all warnings;
   that single line takes the tree total from ~27.8k to ~183. Ratchet
   `suspicious_ptr_cast` down to whatever remains, with written
   justification for any residue — the gate is the compiler, not the
   regex (`phase0.md` §1 note). Gate: clean full rebuild emits **zero**
   C4311/C4302.
6. **Save/load + smoke (manual, needs mounted content).** Datadesc /
   pointer-width audit (FIELD_POINTER, packed offsets, `sizeof`-dependent
   serialization), same-arch x64 save/load run, x86-incompatibility
   documented per decision 2; smoke checklist items 6, 11, 12 executed
   against HL2 content and recorded in `phase0.md` §3.

## Stage 2 execution notes (32-bit target removal)

**`wscript`** (7 removals, guarded by an `ast.parse` syntax check): the
`-4/--32bits` option; the `TARGET32` → `MSVC_TARGETS = ['x86']` branch
(base line re-commented to x64-only); the `BIT32_MANDATORY` assignment
plus the `force_32bit` tool load; and four x86-only flag arms — `'x86'`
dropped from the `-march=core2` and `-mfpmath=sse` lists (now
`== 'x86_64'`, byte-identical behavior for every surviving platform),
the `/arch:SSE` ternary collapsed to plain `/arch:AVX` (the string x64
already received), and the `DEST_CPU == 'x86'` → `COMPILER_MSVC32`
define (never fires on x64 — the source-side `#ifdef COMPILER_MSVC32`
guards stay inert and untouched, a possible later source sweep with no
Gate 2 coverage need).

**CI: 7 jobs deleted** — `build-linux-i386`, `build-windows-i386`,
`build-dedicated-linux-i386`, `build-dedicated-windows-i386`
(confirmed duplicate: byte-identical `-T debug -d` command to its amd64
twin, so the "i386" name was simply wrong), `build-android-armv7a`
(decision 1), `tests-linux-i386`, `tests-windows-i386` (its configure
was the last `--32bits` invocation outside docs). Six 64-bit build jobs
and three test jobs remain — this commit's CI run exercises exactly
that matrix.

**Scripts**: deleted `build-ubuntu-i386.sh`, `tests-ubuntu-i386.sh`,
`build-android-armv7a.sh`, and `waifulib/force_32bit.py` (its only
loader was the removed wscript branch — a 32-bit enforcement tool with
no consumer left). `deploy.sh` de-i386'd: no
`dpkg --add-architecture i386`, no `:i386` package qualifiers, no i386
`PKG_CONFIG_PATH` — native amd64 packages and a plain configure.

**Docs**: `phase0.md` §2's "32-bit (current default …)" block was
stale — the wscript default was always x64 — so the 64-bit block is now
the only block, and the script/CI lists were updated to the
64-bit-only reality.

Verification: `--32bits` survives only as historical mentions in
`phase0.md` / `phase2.md` / one wscript comment; `TARGET32` /
`BIT32_MANDATORY` / `force_32bit` / deleted-script names = docs-only;
`i386` across `.github/**` + `scripts/**` = **0**; wscript parses;
lint **PASS** (10 ids, every baseline untouched); reconfigure success
(`Target CPU: amd64`, "Testing 64bit support: yes"); full build green
(8m20s) with the warning total **exactly 27,809 — no regression**
(C4311/C4302 still 92, Stage 5's worklist).

Kept deliberately: waf's `xcompile.py` / `--android` tooling
(infrastructure with no CI consumer now), XP-era
`MSVC_SUBSYSTEM = 'WINDOWS,5.01'` (not gate-critical), the amd64 `masm`
load (`.asm` sources are not inline `__asm`), and all aarch64/arm flag
handling (decision 3's 64-bit-only reading).

## Stage 3 execution notes (`inline_asm` 127 → 0)

**Method.** The 127 hits were classified by walking each site's full
`#if`/`#elif`/`#else` guard stack rather than by text pattern, then every
edit was specified as a line range plus verbatim substring assertions on
the surrounding directives. One two-phase Python script applied them:
phase 1 re-reads each target file and checks every assertion against the
**pristine** source, refusing to write *anything* unless all **95 edits
across 31 files** verify; phase 2 then applies each file's edits
descending by line so earlier line numbers stay valid. The phase split is
the point — it converts line drift from a silent mis-edit into an abort.

Three classes of edit:

* **dead-everywhere blocks** (the majority): i386-only arms behind
  `_WIN32 && !_WIN64`, `_M_IX86`, `__i386__`, `PLATFORM_WINDOWS_PC32` or
  `COMPILER_MSVC32` — all false on every 64-bit target — deleted, with the
  now-constant guard unwrapped to whichever arm survives;
* **GNU-syntax asm that still assembles on x64** (`luaconf.h`'s
  `lua_number2int` trick): replaced with C or intrinsics, because the lint
  id counts these even where a GNU toolchain would accept them;
* **comment-resident / `#if 0` corpses**: deleted wholesale with their
  enclosing dead region.

**Notable calls, with rationale.**

* `ThreadPause()` (`public/tier0/threadtools.h`) now calls `_mm_pause()`,
  reached through a new guarded include:
  `#if !defined(_MSC_VER) && (defined(__i386__) || defined(__x86_64__))` →
  `<immintrin.h>` (MSVC already gets it via the existing `<intrin.h>`
  under `COMPILER_MSVC64`). `__builtin_ia32_pause` was the first choice
  and was rejected: Clang treats it as an unknown builtin (curl issue
  #9058), so it would have broken the Linux/macOS CI jobs. `_mm_pause()`
  is guaranteed and is already this repo's idiom
  (`thirdparty/SDL-src/src/atomic/SDL_spinlock.c`), so the GCC and MSVC64
  arms share it. The `#elif defined(POSIX)` → `sched_yield()` arm is
  retained so macOS **arm64** still compiles (no x86 intrinsic there);
  the MSVC32 / X360 arms are gone.
* `SetupFPUControlWord()` (`public/tier0/platform.h:790`) collapses from a
  7-branch x87/GCC/Clang ladder to an empty body. Justification: the
  `COMPILER_MSVC64` arm was *already* a no-op, `CHECK_FLOAT_EXCEPTIONS`
  is commented out at the call site's own `#define`, and on x86-64 GCC/
  Clang use SSE (`-mfpmath=sse`) for `float`/`double`, so the x87 control
  word it was twiddling does not govern any arithmetic that survives.
* `tier0/cpumonitoring.cpp` loses its entire `#ifdef PLATFORM_WINDOWS_PC32`
  implementation (360 lines) plus the closing `#endif`; only the `#else`
  stubs remain and the file is now 37 lines. `PLATFORM_WINDOWS_PC32` is
  defined *only* in the `_WIN32 && !_WIN64` branch of `platform.h`, so
  that code was dead on every 64-bit target and the stubs are what has
  always linked.
* `engine/audio/snd_mix.cpp`: four `#if !id386` wrappers unwrapped —
  `id386` is unconditionally 0 on x64, so the C path was already the only
  path taken (behavior-preserving, no audio change).
* `tier0/memdbg.cpp`: the `USE_STACK_WALK` region goes — that macro is
  commented out in the same file, so the stack-walking arm never built.
* `utils/vmpi/vmpi_launch.cpp`: the rdtsc `__asm` became
  `CCycleCount::Sample()/GetMicroseconds()` (mirrors `vmpi.cpp`), plus a
  `tier0/fasttimer.h` include.
* `external/vpc/tier0/threadtools.cpp`'s `xchgl` lock became
  `__sync_lock_test_and_set()`, matching how the GNU arm already worked.

**One apply-script defect, found and repaired (recorded for honesty).**
Three of the 95 edits deleted an `#else`…`#endif` **tail** while leaving
the parent `#if defined(_WIN64)` in place — `tier0/threadtools.cpp` edit 4
and `external/vpc/tier0/threadtools.cpp` edits 3 and 4. Each lost its
`#endif`, and MSVC aborted the tier0 compile with
`C1070: mismatched #if/#endif pair`. The verification I had done asserted
the *start* and *end* directives of each range but never counted directive
*balance inside* it; that is the hole the bug got through. A standalone
balance checker over all 31 edited files found exactly those three (extra
`#if`s are reported as unclosed entries; a stray `#endif` anywhere would
also have been reported, and none was — so the three were provably the
complete set). The repair deletes just the stranded `#if defined(_WIN64)`
line in each case, identified structurally by its unmistakable shape —
`#if defined(_WIN64)` / `return …` / `}` with no `#endif` before the
brace. That leaves the `return` unconditional, which is correct on a
64-bit-only build since `_WIN64` is the only arm that was ever taken.
Re-running the balance checker afterwards reports **0 unbalanced across
all 31 files**.

**Residue deliberately left (out of lint scope).** The lint pattern
requires `{` on the *same* line as `_asm`, so it does not see MSVC's
`_asm` + newline + `{` form. 21 such lines survive in first-party source
(`dedicated/sys_ded.cpp`, `tier0/cpu.cpp`, `tier1/processor_detect.cpp` ×5,
`engine/sys_dll2.cpp`, `studiorender/r_studiodraw.cpp` ×3,
`game/server/hl2/npc_manhack.cpp`, `public/mathlib/mathlib.h`,
`public/tier0/{K8,P4}PerformanceCounters.h`, `mathlib/sse.cpp` ×3, plus
their `external/vpc` twins). Every one sits inside a guard that excludes
Win64 — baseline CI is green with them present — so they compile nowhere
in the 64-bit matrix; they are Stage 5/6-adjacent cleanup, not Gate 2
blockers. `ivp/**` and `thirdparty/**` also contain `__asm` but are
outside the lint scope entirely (vendored / pinned), same as before.

**Stage 3 verification.**

* lint: `inline_asm` = **0** (was 127); all nine other ids untouched.
* preprocessor balance: **0 unbalanced across all 31 edited files**.
* full x64 build green, 7m27s, **0 errors**, 2214/2214 tasks.
* warning total **27,739** — at or below the ratchet. (It is not directly
  comparable to the 27,809 baseline because this build *skipped* `ivp`:
  waf keys staleness on md5, and no header `ivp` includes was among the
  edited set, so ivp's objects were reused and its warnings never
  re-emitted. Forcing an ivp-only rebuild measured its profile exactly —
  **70 warnings, all of them 68 × C4311 + 2 × C4312, zero other codes,
  zero errors** — and 27,739 + 70 = **27,809**, the baseline to the digit.
  Per-code: first-party C4311 24/24 and C4312 27,697/27,697 are unchanged,
  and C4291/C4477/C4273/C4838 stay 11/3/3/1. So Stage 3 introduced **no
  new warnings anywhere**: the deleted code was never compiled on 64-bit,
  so removing it could not move the count either way.)

## Stage 4 execution notes (`win32_long_no_ptr` → 0)

**The detector was wrong, so it was fixed first.** The lint pattern was
`(Get|Set)WindowLong([AW])?\(` — a call paren *immediately* after the
name. `utils/mxtk` spells its calls `GetWindowLong (hwnd, GWL_USERDATA)`
with a space, so **52 truncating calls were invisible to the gate**: the
id read 63 while the real population was **115 across 38 files**. A
formatting quirk was defeating the detector, so per maintainer decision
the pattern is now `(Get|Set)WindowLong([AW])?[[:space:]]*\(`. The widened
pattern still cannot match the *fixed* form — `GetWindowLongPtr(` fails
at the `\(` because `P` is neither `[AW]` nor whitespace — so it counts
only work still to do, and cannot be satisfied by renaming. The
baseline was re-ratcheted 63 → 115 → 0, so the final committed value is 0
and the intermediate 115 is the auditable record of what the id actually
measures.

**Classification drove the edit — not a blind rename.** Each of the 115
sites was classified by its index argument:

| class | n | transformation |
|---|---|---|
| `VALUE` — `GWL_STYLE` / `GWL_EXSTYLE` | 54 | rename the function only. The `Ptr` accessors accept value-sized indices and widen harmlessly; **the index name must not change.** |
| `PTR` — `GWL_USERDATA` / `GWL_WNDPROC` / `GWL_HINSTANCE` / numeric | 59 | rename function **and** index to `GWLP_*`, plus `(LONG)` → `(LONG_PTR)`. |
| `MIXED` — two calls on one line (`Surface.cpp` 2386/2390) | 2 | both calls are `GWL_EXSTYLE`, so both are renamed and the index is kept. |

The cast widening is uniformly safe because on every `PTR` line the
`LONG` is carrying a pointer — `this`, `exp`, `PhonemeBtnProc`, or a `0`
stashed as user data — so `(LONG)` → `(LONG_PTR)` removes the truncation
rather than papering over it. `VALUE` lines are left alone: their `LONG`
value widens into the `LONG_PTR` parameter by itself.

Sites needing individual care: `engine/ccs.cpp` uses a **bare numeric
index `0`** to stash `this` (a window-extra-memory byte offset, valid for
both accessors) — the index is preserved and only the cast widened;
`phonemeproperties.cpp:210` stores a subclassed window proc, so
`GWL_WNDPROC` → `GWLP_WNDPROC` with `(LONG)PhonemeBtnProc` →
`(LONG_PTR)`; `mxwindow.cpp:72` stores `0` explicitly.

**Verification split.** Only **11 of the 115 are compile-verifiable** —
`engine` 8 (`sys_getmodes` 5, `ccs` 2, `sys_mainwind` 1) and
`vgui2/src/Surface.cpp` 3. `mxtk`, `hlfaceposer`, `scenemanager`,
`hammer`, `hlmv`, `vmpi` and `devtools/WiseInstallerHelpers` are **not in
`wscript` at all**, so the other 104 are verified by lint + line-level
review, the same treatment Phase 1a gave unbuilt trees (decision 4 still
holds: lint counts the whole tree regardless of what waf compiles).

Method: the two-phase apply returned `phase 1 OK: 115 sites across 38
files`, where phase 1 re-reads each file, re-derives hits with the widened
pattern, and asserts per-file that **no unfixed call remains after
transform** — that post-condition is what makes "0" trustworthy rather
than merely reported. Files are round-tripped through
`surrogateescape` so a Windows-1252 `©` byte in one of them survives
byte-for-byte.

**Stage 4 verification.**

* lint: `win32_long_no_ptr` = **0** (detector widened, baseline
  63 → 115 → 0); `inline_asm` still 0; all other ids untouched.
* full build green, **0 errors**. Exactly the 4 built files I touched
  recompiled (`engine/ccs.cpp`, `engine/sys_getmodes.cpp`,
  `engine/sys_mainwind.cpp`, `vgui2/src/Surface.cpp`); the 104 unbuilt
  tool sites are verified by lint + line-level review per decision 4.
* warning-neutral, measured not assumed: those 4 files emitted **0
  warnings before and after**, all 53 warnings in this run come from
  `public/tier1/utlmemory.h` (the known 99.4% source), and **no warning
  mentions `WindowLong`/`Ptr`**. C4244 appears nowhere in this run *or*
  in the full Stage 3 log, so the `LONG_PTR` → `DWORD` narrowing at
  `sys_getmodes.cpp:1367/1368` and `sys_mainwind.cpp:651` is accepted
  silently — and is value-preserving regardless, because Windows stores
  styles as a 32-bit `DWORD`, so truncating a `LONG_PTR` that holds one
  cannot change the value.
* **the `C4311 = 0` visible in this particular log is an incremental-build
  artifact, not an achievement** — the 24 first-party C4311 sites live in
  files this run did not recompile (`InputWin32.cpp` 19, `Sys_Utils.cpp`
  2, `voice_mixer_controls.cpp`, `baseentity.cpp`,
  `vguimatsurface/Input.cpp`). Those remain Stage 5's worklist unchanged;
  none of them is a `WindowLong` call, so Stage 4 neither fixed nor
  regressed them.

## Stage 5 execution notes (pointer-width warnings → 0)

**Why the two codes were so lopsided.** Stage 1's baseline recorded 24
first-party C4311 (a pointer narrowed to a 32-bit type) against 27,697
first-party C4312 (a narrow integer widened back into a pointer), plus 70
more inside the `ivp` submodule (68 C4311 + 2 C4312). Almost the whole
C4312 mass traces to one line: `public/tier1/utlmemory.h` defines
`UTL_INVAL_SYMBOL` as an `int`-sized sentinel that essentially every
pointer debug cast in the codebase routes through, so a single wrong-width
literal accounted for **27,626** of them. Widening that literal resolves
all 21 memhandle call sites at once. That is the correct shape of the fix
— widen the shared field — rather than laundering the same truncation at
each individual use site, and it is why the C4312 column collapsed in one
edit while C4311 needed twenty-four separate ones.

**The 24 first-party C4311, by file, and what each one needed.**

| file | n | transformation |
|---|---|---|
| `vgui2/src/InputWin32.cpp` | 19 | six method signatures, two returns, one `p->` and sixteen `item->` reads widened to `uintp`, matching the `IInput.h` virtuals and struct fields they serve. |
| `gameui/Sys_Utils.h` | 2 | `typedef int WHANDLE` became `typedef intp WHANDLE`, with `tier0/platform.h` included so the typedef is self-contained. |
| `engine/audio/voice_mixer_controls.cpp` | 1 | two-step `(UINT)(UINT_PTR)`, **kept on purpose** — see below. |
| `game/server/baseentity.cpp` | 1 | `%08lx` plus cast became `%p`. |
| `vguimatsurface/Input.cpp` | 1 | the producer stopped stuffing an `HWND` into `event.m_nData` at all. |

**Where a two-step cast is the honest answer, and where it is a fig
leaf.** The rule applied throughout Stage 5 is that a two-step cast is
acceptable only when the value provably cannot be widened, and wrong the
moment it is hiding a round-trip that used to be broken.
`voice_mixer_controls.cpp` is the legitimate case: the Windows SDK
`mmeapi.h` really does declare `mixerGetDevCaps(UINT uMxId, ...)`, so the
parameter is genuinely a 32-bit device id and no pointer ever crosses it —
`(UINT)(UINT_PTR)` is a bit-identical value moving through a
misdescribing intermediate, and it is written out with that justification
in a comment. `game/server/baseentity.h` `SetMoveDone` is the other
edge: `(void *)(intp)(...)` is width-only at all 43 call sites, because
`int` to `intp` and `int` to `void *` sign-extend identically, so the fix
is one cast rather than a new field type. `tier1/datamanager.h` gets its
fix one level up too: `memhandle_t` is a `FORWARD_DECLARE_HANDLE` (i.e. a
pointer), so widening `INVALID_MEMHANDLE` to `(uintp)0xffffffff` makes all
21 of its sites narrow consistently through the macro instead of each one
re-deriving the cast.

**The IME ABI was genuinely broken on x64, not merely unpretty.**
`IInput` enumerated IME language, conversion and sentence modes through
`int handleValue` fields while the underlying `HIMC`/`HKL` values are
pointer-sized, so on x64 any consumer read back a sign-extended low 32
bits — a real round-trip failure. The three struct fields, the two getter
returns and the three `OnChangeIMEByHandle`-family parameters are now
`uintp` throughout, and the matching `TEXTENTRY_IME_*` handlers moved to
the already-present `MESSAGE_FUNC_UINT64` / `SetUint64` dispatch path so
`KeyValues` carries the full value end to end. No include had to be added
to the public headers for this: `uintp` is already visible through
`vgui/VGUI.h`, which `IInput.h`, `TextEntry.h` and `InputWin32.cpp` all
include directly.

**`vguimatsurface/Input.cpp` deliberately did not widen `m_nData`.**
That field is named and read as a generic 32-bit payload all over the
file, so widening it would have started a wave of narrowing elsewhere.
Instead the producer that was stuffing an `HWND` into it now records the
handle in a file-scope variable that the `IE_IMESetWindow` consumer reads
directly, which removes the round-trip without touching the struct layout
that other message types depend on.

**Residues left in place, and why.** `materialsystem/cmaterial.cpp:3567`
still casts `0xffffffff` to `ITexture *` as a sentinel; the extension's
reload semantics around that magic value have not been audited, so
changing it could silently alter behaviour, and it is a single warning
outside this stage's gate. `external/vpc/public/tier1/utlmemory.h:432`
holds the untouched twin of the utlmemory fix; it is not in the warning
baseline and touching it risks the standalone VPC tool build for no
measured gain. On the lint side, `suspicious_ptr_cast` moves **45 -> 29**:
the 16 removed are the truncations fixed here, and each of the remaining
29 is a pointer deliberately narrowed to compare against a documented
magic constant — none of them can reach storage.

**A pre-existing defect was found and deliberately not fixed.**
`vgui2/vgui_controls/TextEntry.cpp:1380` builds a sentence-mode menu item
from `modes[i]` while labelling it with `sentencemodes[i].menuname`, and
dispatches `"DoConversionModeChanged"` where `TextEntry.h:125` expects
`"DoSentenceModeChanged"`. This is a copy-paste bug that predates Phase 2;
the Stage 5 rewrite preserved `modes[i]` exactly rather than fixing it,
because it is a behaviour change with no bearing on pointer width and
belongs to whoever owns the sentence-mode feature.

**`ivp` was forked rather than left dirty.** The submodule contributed
exactly 70 warnings, and `scripts/lint-legacy.ps1` structurally cannot see
them — it greps tracked files, and submodules are outside that set. Since
upstream is not ours to patch, `nillerusr/source-physics` was forked to
`mistersliva/source-physics`, the casts fixed there (63 lines across 13
files, `47533475..318b93f`), and `.gitmodules` repointed. The fork is
public, so CI's `git submodule init && git submodule update` fetches it
unauthenticated exactly as it did the upstream URL. The dominant pattern
was `(long)somePointer` feeding a `%lx`, which truncates on Win64 where
`long` is 32-bit but happens to be pointer-sized on LP64 — which is why
this never surfaced before the port. Each was widened through ivp's own
`intp` before the final `(long)` narrowing, so the value every `%lx`
receives is bit-identical on both ABIs. Three sites needed individual
treatment: `ivu_set.hxx` hashed a set key straight to `long` and now also
includes `ivu_types.hxx` instead of relying on its includer's include
order; `ivp_surbuild_pointsoup.cxx` stores an integer index in a
pointer-typed set slot; and `ivp_gridbuild_array.cxx:840` **was a real
64-bit bug rather than a warning** — it aligned a destination pointer by
truncating it to `long` first, which on Win64 yields an address outside
the buffer it came from, and now aligns through `uintp`. Casts sitting
inside `#ifdef DEBUG` were converted too, so enabling a debug flag cannot
quietly reintroduce a truncation. Two `(long)` sites remain untouched on
purpose: an integer `fseek` offset in `3dsimport_load.cxx`, which is not
listed in any `wscript` source list and is not a pointer, and one inside a
comment.

**Stage 5 verification — measured on a clean rebuild, not an incremental
one.** Emptying `build/` (keeping `c4che`) and rebuilding ran **2214
tasks**, 2161 of them compile steps, in 5m03s with **0 errors**:

| code | Stage 1 baseline | after Stage 5 |
|---|---|---|
| C4311 | 92 (24 first-party + 68 `ivp`) | **0** |
| C4302 | 0 | **0** |
| C4312 | 27,699 (27,697 first-party + 2 `ivp`) | **1** — only `cmaterial.cpp:3567`, above |
| C4291 / C4477 / C4273 / C4838 | 11 / 3 / 3 / 1 | 11 / 3 / 3 / 1 (untouched) |
| **total** | **27,809** | **19** |

Every one of the 19 is a pre-existing warning of a class this phase never
touched (AI `operator new`/`delete` mismatch, printf format-type mismatch,
`Sleep` dll linkage, a `char` narrowing). Lint confirms
`inline_asm = 0`, `win32_long_no_ptr = 0`, `suspicious_ptr_cast = 29`,
`dx_to_gl_abstraction = 215`, `d3d9_com_types = 924`, `local_minmax_macro
= 6`.

**A defect the Windows-only build could not see.** The first push of Stage
5 passed a full local MSVC build and then **failed CI on Linux and macOS**
with `use of undeclared identifier 's_hLastHWnd'`. The variable the new
`IE_IMESetWindow` consumer reads sits inside the file's `#ifdef WIN32`
block, and the consumer did not. The message is posted nowhere except by
that Win32 window procedure, so the fix was to compile the forward under
the same `WIN32` guard — a no-op with a comment on other platforms, and
bit-identical behaviour on Windows. The commit was amended rather than
followed up, so the Stage 5 commit compiles on every platform in the
matrix. The general lesson stands: a green local build on one platform is
not evidence about the other five jobs.

## Stage 6 execution notes (save/load audit + x86-save enforcement)

**What the audit found.** The save path was walked serializer by
serializer to answer one question: which records put raw pointer bytes
on disk? Almost nothing does. `FIELD_FUNCTION` writes the function's
*name* (`UTIL_FunctionToName` / `UTIL_FunctionFromName` round trip),
`string_t`, `CUtlSymbol`, activity and variant-string fields serialize
string *content*, entity/edict/class pointers become `int` indices, the
engine's own header structs are `int`-only, the token table is a
byte blob whose `char *` index is rebuilt in memory on load,
`TD_OFFSET_PACKED` offsets are computed at restore time, and every one
of the 119 `DEFINE_CUSTOM_FIELD` ops writes counts, indices or names.
Raw `sizeof(void*)` records appear at exactly two places:
`physics_saverestore.cpp:46` (a `FIELD_POINTER` inside a
header-prefixed block, where `ReadSimple`'s `MIN`/skip tolerance degrades
a width mismatch gracefully) and the vphysics pointer-association blobs —
`vphysics_saverestore.cpp` writes at 60/138/196 and reads at 108/159/212.
The vphysics reads pass `nBytesAvailable = 0`, the one path that takes an
unconditional native `sizeof(void*)` read; that is the concrete incompat:
a 4-byte pointer from an x86 file consumed as 8 bytes on x64 shifts the
stream for every record after it, desyncing mid-restore instead of
failing cleanly. The pointers themselves are opaque remap keys
(`s_VPhysPtrMap`) that are never dereferenced, so a **same-arch** x64
save round-trips correctly — the defect is purely cross-arch. Also
checked: `CRestore::ReadFunction`'s `GNUC` arm copies `sizeof(void*) * 2`
for member-function pointers, correct because waf force-defines `GNUC`
on every POSIX target (`wscript:224/235/255/269`).

**Enforcement: the version tag now carries pointer width.** There was no
arch discriminator at all — `SAVEGAME_VERSION` was `0x0073` on both
arches, so an x86 save passed the check at `host_saverestore.cpp:901`
and then corrupted. `public/savegame_version.h` now defines
`SAVEGAME_VERSION_BASE` (`0x0073`), `SAVEGAME_VERSION_ARCH_FLAG`
(`0x8000`), and `SAVEGAME_VERSION = base|flag` on 64-bit builds (the
same compiler-macro test `tier0/platform.h` uses for `PLATFORM_64BITS`,
plus waf's force-define, so the header needs no includes). One macro
change covers every writer (`host_saverestore.cpp:812`, `:1278`) and
every checker (`:901`, `:1764`, `:2240`, `gameui/BaseSaveGameDialog.cpp:537`)
because all of them compare equality against the single macro. When the
tag matches the base but lacks the stamp, `SaveReadHeader` prints a
tailored warning naming the 32-bit origin instead of a bare version
mismatch; GameUI's own check is untouched (silent `return 0`, the save
simply doesn't get a name in the list). No x64 save files exist yet —
the engine has never booted with mounted content — so re-stamping
invalidates nothing real.

**A stale comment claimed the opposite.**
`game/shared/saverestore.cpp:1287` said `FIELD_FUNCTION` "just write[s]
the address out"; the code writes the function's *name* through
`UTIL_FunctionToName`, which is precisely why that field never had a
pointer-width problem. Comment corrected to match.

**A defect found and deliberately not fixed.**
`CThinkContextsSaveDataOps::MakeEmpty` (`game/server/baseentity.cpp:1734-1738`)
assigns `NULL` to a *local copy* of the member pointer, so it clears
nothing. It predates Phase 2, bears on none of this phase's gates, and
belongs with the `TextEntry.cpp:1380` record above: noted, not touched.

**Gate 2 save/load criteria, restated.** Checklist 6 (save + load round
trip) and 11 (64-bit process) must pass on x64 — that is the same-arch
path the audit shows is sound. Checklist 12 flips meaning: a 32-bit save
must now be *rejected at the header* with the tailored warning, and the
incompatibility plus audit rationale is documented in this section —
which is also what satisfies `phase0.md` §3 item 12's "migration path is
documented" clause, by recording why there is deliberately no migration.

## Gate 2 completion criteria

- CI matrix is 64-bit-only (no i386, no armv7 Android) and green;
- lint: `win32_long_no_ptr = 0`, `inline_asm = 0`;
- a clean full x64 build emits **zero C4311/C4302** warnings;
- save/load works on x64 (checklist 6 + 11); x86-save incompatibility
  documented (checklist 12);
- warning-count baseline recorded in `phase0.md` §2 and not regressed.

**Status: MET, 2026-09-24 at `6641b4ca`.** CI 6/6 green and 64-bit-only;
lint both 0; clean `-T release` rebuild C4311 = 0, C4302 = 0 (total 11
warnings, ceiling holds — config switch and rationale in
`phase0.md` §2); save/load round trip passed with a 0x8073-stamped
header (checklist 6); 27/27 binaries `8664` (checklist 11); x86-save
rejection captured with the tailored warning (checklist 12,
`console_smoke12.log`). Results and evidence paths recorded in
`phase0.md` §3.
