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

## Gate 2 completion criteria

- CI matrix is 64-bit-only (no i386, no armv7 Android) and green;
- lint: `win32_long_no_ptr = 0`, `inline_asm = 0`;
- a clean full x64 build emits **zero C4311/C4302** warnings;
- save/load works on x64 (checklist 6 + 11); x86-save incompatibility
  documented (checklist 12);
- warning-count baseline recorded in `phase0.md` §2 and not regressed.
