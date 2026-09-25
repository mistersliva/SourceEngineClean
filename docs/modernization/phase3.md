# Phase 3 — Graphics: DX11 + Vulkan (drop FFP / DX6-9 / togl / dx9sdk)

Part of the Gate 3 plan ([phase0.md](phase0.md) §4). Scoping completed
against `7521d6c4` (the commit that closed the Phase 2 debug-boot
follow-up: two boot blockers plus the two debug-only defect fixes).

## Key scoping finding: there is already a backend seam — and it speaks D3D9 everywhere

The renderer is already split at a clean boundary; Phase 3 replaces what
lives *under* it rather than inventing a new layer:

1. `CMaterialSystem::SetShaderAPI()` (`materialsystem/cmaterialsystem.cpp:601`)
   loads a **shader-API DLL by name** and resolves `IShaderDeviceMgr` from
   its factory. The name comes from `launcher/launcher.cpp:721`
   (`"shaderapidx9" DLL_EXT_STRING`, overridden only by `-noshaderapi`),
   `appframework/VguiMatSysApp.cpp:60` and `hammer_launcher/main.cpp:117`
   (tools and faceposer/hammer); every content tool passes
   `shaderapiempty` instead.
2. `materialsystem/shaderapidx9/` — **60 files** — implements that seam
   (`IShaderDeviceMgr` / `IShaderDevice` / `IShaderAPI` / `IShaderShadow`,
   see `CShaderDeviceMgrBase` in `shaderdevicebase.h`) in D3D9 terms:
   `dx9hook.h` wraps `Direct3DCreate9`, `shaderdevicedx8.*` owns the
   device, `texturedx8.*` / `vertexshaderdx8.*` / `TransitionTable.*` own
   resources and state.
3. On POSIX the *same D3D9-shaped* call surface is not rewritten — it is
   **translated to OpenGL** by `togl/` (`togl/linuxwin/dxabstract.cpp`,
   5300+ lines) whenever `DX_TO_GL_ABSTRACTION` is defined —
   `wscript:199-204` adds it whenever `conf.env.GL` is set, and
   `--use-togl` (`wscript:305`) defaults that to **on for every
   non-Windows platform** (`conf.env.GL` itself: `wscript:183`).
   `togles/` is the
   OpenGL ES variant for the Android target Phase 2 Stage 2 already
   deleted from CI.
4. `dx9sdk/` supplies the headers plus `d3d9.lib` / `d3dx9.lib` /
   `dxguid.lib` (`wscript:429/430/432`, LIBPATH at `wscript:567`) and sits
   on the shader projects' include paths (`stdshader_*.vpc`:
   `..\..\dx9sdk\include`).

So the roadmap line "new RHI (DX11 + Vulkan)" resolves to: **keep the
existing seam as the RHI boundary, replace its implementation with one
first-party D3D11 backend, and delete both D3D9 shims.** Nothing above
the seam (`cmaterialsystem`, materials, engine) changes shape; everything
below it is rewritten or deleted.

## Gate 3 criteria (phase0.md §4)

- `d3d9_com_types` and `dx_to_gl_abstraction` at **0**;
- `togl/`, `togles/`, `stdshaders/*_dx6|dx7|dx8`, `dx9sdk/` **deleted**;
- boots on **DX11**;
- **Vulkan backend at parity**;
- plus checklist items 13 (backend reports `D3D11` / `Vulkan`) and 14
  (screenshot comparison against the DX9 baseline).

## Inventory (scoping at `7521d6c4`, lint baselines)

Counts produced with the lint's own mechanics (`git grep -I -c -E`, the
exact patterns in `scripts/lint-legacy.ps1`), so they are directly the
numbers the gate will read.

| Category | Count | Distribution / notes |
|---|---|---|
| `d3d9_com_types` | **924** hits / 38 files | `togles` 224, `togl` 224, `public` 175 (`public/togl/linuxwin` 8 files, `public/togles/linuxwin` 8, `public/openvr` 1), `materialsystem` 154 (12 of them `shaderapidx9`), `dx9sdk` 146, `engine` 1 |
| `dx_to_gl_abstraction` | **215** hits | `materialsystem` 138 (**119 in `shaderapidx9`**, across 16 files — `vertexshaderdx8.cpp` 27, `shaderapidx8.cpp` 22, `shaderdevicedx8.cpp` 20, `d3d_async.h` 13, `texturedx8.cpp` 11, …), `public` 26, `appframework` 19 (`sdlmgr.cpp`), `engine` 10, `togl` 6, `togles` 6, `gameui` 5, `bitmap` 2, `vgui2` 2, `tier2` 1 |
| `togl/` | 21 files / 888 KB | plus `public/togl/` headers — **4 directories** total (`togl`, `togles`, `public/togl`, `public/togles`) |
| `togles/` | 23 files / 959 KB | plus `public/togles/` |
| `dx9sdk/` | **208 files / 36.7 MB** | headers + `lib/amd64` + `lib/x86` |
| legacy shader projects | **56 files** matching `_dx6`/`_dx7`/`_dx8` in `materialsystem/stdshaders`, plus `stdshader_dx6.vpc`, `stdshader_dx7.vpc`, `stdshader_dx8.vpc` | `stdshader_dx9.vpc` / `stdshader_dbg.vpc` stay (renamed later) |
| D3D9 *enum* surface (not linted) | `D3DRS_` 1335, `D3DFMT_` 785, `D3DCMP_`/`D3DBLEND_` 258, `D3DTSS_` 187, `D3DLIGHT`/`D3DMATERIAL9` 169, `D3DSAMP_` 129, `D3DPT_` 79, `D3DTS_` 77 | the real porting volume; the lint only counts COM *types* |
| fixed-function state users | `shaderapidx9/{TransitionTable,shaderapidx8,shadershadowdx8,d3d_async,dx9hook,locald3dtypes,stubd3ddevice}`, `utils/scratchpad3dviewer`, `utils/smp` | FFP lives almost entirely inside the backend being replaced |
| shader profiles today | **SM1–3**: `devtools/bin/fxc_prep.pl` picks `ps_1_1…ps_3_0` / `vs_1_1…vs_3_0` | D3D11 requires SM4+; the tool stays, the targets change |
| runtime shader compile | `D3DXCompileShader`, `D3DXCompileShaderFromFile` (`vertexshaderdx8.cpp:914/961/1802`) | D3DX dies with `dx9sdk/` → must move to `d3dcompiler` |
| `d3dcompiler` references | **0** | not linked anywhere yet; Windows ships `d3dcompiler_47.dll` |

Warning gate unchanged: a clean `-T release` rebuild stays at the
`phase0.md §2` baseline (**11 warnings**; the ceiling is 27,809 and no
new warning class may be introduced).

## Maintainer decisions

1. **Native DX11 + DXVK translation (confirmed 2026-09-24).** One
   first-party D3D11 backend. Windows links the real `d3d11.dll` /
   `dxgi.dll`; Linux builds the *same* backend against **DXVK-Native**,
   which translates those D3D11 calls onto Vulkan. The engine only ever
   speaks D3D11, so there is one code path to get right and the fxc
   shader pipeline survives largely intact. DXVK-Native is a proven
   pattern (Terraria ships exactly this) and its README states the
   integration explicitly: it "replaces certain Windows-isms with a
   platform and framework-agnostic replacement, for example, `HWND`s can
   become `SDL_Window*`s", selected with `DXVK_WSI_DRIVER` (`SDL3`,
   `SDL2`, `GLFW`). Source's POSIX windowing already is SDL
   (`appframework/sdlmgr.cpp`), so the window side lines up.
   **Rejected:** a bgfx backend; two separately maintained native
   backends (DX11 *and* Vulkan) — both considered and declined by the
   maintainer at scoping time.
2. **The RHI is the existing `IShader*` seam (scoping call).** A new
   thin RHI header would force `cmaterialsystem`, `stdshaders` and the
   engine to be rewritten around an interface that already exists and is
   already backend-agnostic except where D3D9 types leak (the 175
   `public/` hits). Those leaks are purged instead of being hidden behind
   a second layer. The seam's method surface is audited in Stage 2 and
   anything with D3D9-shaped semantics is re-specified there, in the
   header, once — not translated twice.
3. **"Vulkan backend at parity" is verified through DXVK-Native, not a
   second renderer (scoping call, needs environment).** Parity therefore
   means: the same backend binary path rendering identical frames on
   Vulkan. Getting there needs a Linux GPU run environment — **this
   machine has `wsl.exe` but no distribution installed** (`wsl -l -v`
   prints usage, `wsl --status` is empty), and CI's Linux jobs compile
   but never run. Stage 10 is gated on provisioning one of: WSL2 +
   distro + Vulkan driver, a Linux box/VM with a GPU, or an accepted
   deferral where Gate 3 records compile-level parity and re-opens the
   run-level check. **Open — maintainer decision required.**
4. **Fixed-function state is removed, not emulated (scoping call).**
   D3D11 has no FFP, so `D3DTS_*` / `D3DTSS_*` / `D3DLIGHT*` semantics
   cannot survive as-is. Stage 6 first proves by call-site audit that the
   runtime path does not depend on them (they are concentrated in the
   backend and in two unbuilt tools), then deletes. Anything that *is*
   live gets re-expressed in the shader pair it belongs to, not
   re-implemented as a compatibility layer.

## Stages

Same workflow as Phases 1a/2: patch →
`powershell -ExecutionPolicy Bypass -File scripts\lint-legacy.ps1 -UpdateBaseline`
→ full local `.\waf.bat build -j 8` → commit(s) → push → gate on fresh CI
(6 jobs). Lint baselines ratchet down at every stage that removes hits;
the phase ends with both ids at **0**.

1. **Baseline captures (the checklist 13/14 "before" side).** On the
   current DX9/GL build: record the backend-name spew, and capture the
   four checklist-14 frames — `d1_trainstation_01` (props),
   `d2_coast_01` (water/reflection), a particle-heavy scene, and the
   HUD/font rendering — into `docs/modernization/baseline_dx9/`. Gate:
   images + backend string committed; nothing else changes.
2. **RHI surface audit → mapping table.** Enumerate every method of
   `IShaderDeviceMgr` / `IShaderDevice` / `IShaderAPI` / `IShaderShadow`
   plus the D3D9 enum surface above, and classify: *D3D11-direct*,
   *needs a semantic change* (constant buffers, vertex input layouts,
   pipeline-state objects, render-target formats), or *dead* (FFP,
   state-block/restoration, D3D9-only queries). Output: a checked-in
   `docs/modernization/phase3_rhi_map.md`. Gate: table covers 100% of
   methods; no code yet.
3. **Device + swapchain spike.** New `materialsystem/shaderapidx11/`
   project behind the existing seam: create device/swapchain, clear,
   present, handle resize/alt-tab/`vid_restart`, and boot to the main
   menu rendering a known clear colour through the real materialsystem
   path. Wire `launcher/launcher.cpp:721` and
   `appframework/VguiMatSysApp.cpp:60` to select it. Gate: boots to menu
   on Windows with no dialog, no regression to the DX9 build (both still
   selectable).
4. **Shader pipeline to SM4/5.** Retarget `fxc_prep.pl`'s profile
   selection and the `buildshaders.bat` / `stdshader_*.vpc` flow from
   SM1–3 to `vs_4_0`/`ps_4_0` (or `vs_5_0`); compile the stock shader set
   and fix the constructs SM4 rejects (SM1–3-era write-mask and
   instruction limits noted in `bik_ps11.psh`, `cloak_ps2x.fxc`,
   `eyes_ps2x.fxc`, …). Replace `D3DXCompileShader*` runtime compilation
   with `d3dcompiler` (`D3DCompile`), which is not linked anywhere today.
   Gate: all stock shaders compile to SM4 and load; `d3d9_com_types` in
   `shaderapidx9`-replacement files stays 0 by construction.
5. **Resources and formats.** `D3DFMT_*` → DXGI formats, textures /
   cubemaps / render targets / depth-stencil, HDR + tonemap, MSAA
   resolve, sRGB; vertex decls → input layouts, dynamic VB/IB
   (`dynamicvb.h` / `dynamicib.h`) → `MAP_WRITE_DISCARD` buffers,
   `d3d_async` predication → D3D11 queries. Gate: `d1_trainstation_01`
   loads and renders geometry with materials.
6. **State pipeline + FFP removal.** `D3DRS_*` / `D3DSAMP_*` /
   `D3DCMP_*` / `D3DBLEND_*` → cached D3D11 blend/depth/rasterizer/sampler
   state objects (`TransitionTable`'s job, re-expressed); constant
   registers → constant buffers following
   `shader_constant_register_map.h`; audit-then-delete `D3DTS_*` /
   `D3DTSS_*` / `D3DLIGHT*` (decision 4). Gate: lighting, water and
   particles correct in the checklist-14 scenes.
7. **Build wiring swap.** `wscript`: drop `conf.check(lib='d3dx9')`,
   `d3d9`, `dxguid` (429/430/432) and the `dx9sdk/lib/...` LIBPATH (567)
   for `d3d11` / `dxgi` / `d3dcompiler`; drop `--use-togl` (305-306),
   `--togles` (317-318), the `if conf.env.GL:` define block (199-204 —
   **carries `BINK_VIDEO` too**, see Risks), the `TOGLES` flag
   (182/206-207) and the `togl`/`togles` project entries
   (457-460, 640-643); point `stdshader_*.vpc` include paths off
   `dx9sdk\include`. Linux links DXVK-Native's `d3d11`/`dxgi`. Gate:
   reconfigure succeeds on Windows and Linux, both build green.
8. **Deletions + lint ratchet.** Delete `togl/`, `togles/`,
   `public/togl/`, `public/togles/`, `dx9sdk/`, and the 56
   `stdshaders/*_dx6|dx7|dx8` files with their three `.vpc` projects;
   unwrap the 7 `DX_TO_GL_ABSTRACTION` hits that live in `gameui/` (5)
   and `vgui2/` (2) — those directories survive until Gate 4, so their
   hits must reach 0 here; unwrap the remaining `public/`, `engine/`,
   `appframework/`, `bitmap/`, `tier2/` sites. Ratchet
   `d3d9_com_types` 924 → 0 and `dx_to_gl_abstraction` 215 → 0 with
   `-UpdateBaseline`. Gate: lint both **0**, tree builds, DX9 build
   no longer exists.
9. **Vulkan parity run (decision 3 environment).** Same binary path on
   Linux through DXVK-Native (`DXVK_WSI_DRIVER=SDL2`), frame-for-frame
   comparison against Windows DX11 on the checklist-14 scenes. Gate:
   parity recorded, or the deferral clause written into Gate 3 evidence.
10. **Smoke 13/14 + Gate 3.** Checklist 13: backend reports `D3D11`
    (Windows) / `Vulkan` via DXVK (Linux) instead of DX9/GL.
    Checklist 14: the four screenshot comparisons against Stage 1's
    baseline, recorded in `phase0.md` §3. Full lint green, warning
    baseline held, CI 6/6 green.

## Stage 1 execution notes (baseline captures, 2026-09-25)

Capture procedure (all four frames): script-driven boots of
`hl2_launcher.exe` with `-condebug -windowed -noborder -novid -nojoy
+map <map>` (working directory `game\`), 140 s load + 15 s settle, a
960x540 client-rect GDI `CopyFromScreen` capture, then `taskkill /F /T`
with pre- and post-run verification that no `hl2*` process survives.

Findings that shaped the procedure:

* **Stale-instance forwarding.** `game\run_hl2_x64.bat` hardcodes its
  argument list, and a leftover launcher could forward a later `+map`
  into an already-running instance - early frames were contaminated
  this way (a `d1_town_01a` shot differed from the clean spawn by 14.6).
  Scripts invoke the launcher directly and kill-verify around every
  boot.
* **Joystick drift.** `JOY_AXIS_X/Y` map to Turn/Look and drifted the
  view in early frames; every capture boot passes `-nojoy`.
* **Input channels.** Synthetic `keybd_event` keys never reach the
  engine even with the window verified foreground
  (`GetForegroundWindow` = game hwnd). `PostMessage(WM_KEYDOWN/UP)` to
  that hwnd *does*: a bound `echo` reached `console.log`, and bound
  `setang` calls rotated the camera (scene diff 53-94 vs baseline).
  Command-line `+commands` after `+map` do *not* run post-load
  (ordering probe: diff 0.13 = unchanged view, no entity created).
* **Particle frame = injected emitters.** The 70 campaign maps contain
  zero `info_particle_system` entities, and every map-authored emitter
  near a spawn is either off (`env_steam` without `InitialState 1`),
  not auto-igniting (`env_fire` without `SF_FIRE_START_ON` 0x4), or
  occluded from every spawn view (checked `d1_town_01`,
  `d1_town_03` indoor spawn, `d1_canals_01`, `d2_coast_09`
  underground, `d3_c17_*`, `d1_trainstation_05`). The frame is
  therefore `d2_coast_11` with two `env_fire` clusters
  (`spawnflags 5` = INFINITE|START_ON, `firesize 50`) injected at the
  player's aim point on open ground via F-key binds in
  `game/hl2/cfg/config.cfg` fired with `PostMessage`. The same bind
  block replays the frame on the DX11 build for checklist 14:
  `sv_cheats 1; setang 30 320` + 3x `ent_create env_fire spawnflags 5
  firesize 50`, repeat at `setang 30 290`, frame at `setang 25 305`.
* **Pre-existing engine crash (record-only).** A session idling ~13
  min on `d2_coast_11` died with `0xc0000005` in `engine.dll+0x55d5c9`;
  `console.log` shows late joystick init and `Redownloading all
  lightmaps` immediately before shutdown. All capture windows are ~3
  min and unaffected.
* **Final inventory.** `baseline_dx9/` holds exactly `backend_dx9.txt`
  plus four frames: `d1_trainstation_01_props.png`,
  `d2_coast_01_water.png`, `d2_coast_11_particles.png`,
  `hud_font.png`.

## Risks and known hard parts (scoping)

* **Constant buffers are the deepest semantic change.** D3D9 hands
  shaders uniform registers (`SetVertexShaderConstantF` /
  `SetPixelShaderConstantF`); D3D11 requires packed, layout-sensitive
  constant buffers. The mapping lives in
  `materialsystem/stdshaders/shader_constant_register_map.h` — Stage 2
  must design the packing rule once, because getting it wrong produces
  *plausible but wrong* rendering rather than an error.
* **SM1–3 shader sources.** Profile retargeting is mechanical; *source*
  compatibility is not guaranteed — SM1-era comments in the stock shaders
  already document workarounds for limits SM4 removed. Budget explicit
  fixes per shader family in Stage 4 rather than assuming fxc "just
  works".
* **D3DX removal is wider than shaders.** `dx9sdk/` also backs
  `conf.check(lib='d3dx9')`; anything else resolving D3DX symbols must be
  found at Stage 7 by link, not by grep.
* **DXVK-Native toolchain gotchas**, straight from its README:
  `__uuidof(type)` works but `__uuidof(variable)` does not (use
  `__uuidof_var(variable)`), and a WSI backend must be chosen via
  `DXVK_WSI_DRIVER`. Windows headers it ships are a slim subset.
* **The GL define block also carries `BINK_VIDEO`.**
  `wscript:199-204` defines three macros together: `DX_TO_GL_ABSTRACTION`,
  `GL_GLEXT_PROTOTYPES` **and `BINK_VIDEO`**. On waf, `BINK_VIDEO` reaches
  a Linux build *only* through this block (the VPC path defines it
  separately for `$LINUXALL`/`$WIN32` in
  `vpc_scripts/source_video_base.vpc:41/43`), and
  `engine/audio/snd_dev_sdl.cpp` guards real code with
  `#if defined( BINK_VIDEO ) && defined( LINUX )`. Deleting the block
  wholesale would silently switch Bink video off on Linux while
  everything still compiles — the define must be hoisted out of the GL
  branch, not deleted with it.
* **`dx9hook.h` / overlay code** wraps `Direct3DCreate9` for injection
  style consumers; decide in Stage 2 whether it lives (rewritten for
  DXGI) or dies with DX9.
* **`public/openvr`** carries one D3D9 device reference — either it is
  updated to D3D11 or the header's use is confirmed dead (VR is out of
  scope; `public/sourcevr/` was deliberately kept at Gate 1).
* **Gate-coverage gap: the lint counts COM *types* only.**
  `d3d9_com_types` matches `IDirect3D*9`, so the ~3.1k D3D9 *enum*
  references (`D3DRS_`, `D3DFMT_`, `D3DTSS_`, `D3DTS_`, …) are invisible
  to it, and the two fixed-function holdouts outside the backend —
  `utils/scratchpad3dviewer` (40 hits across `d3dapp.cpp` /
  `scratchpad3dviewer.cpp`) and `utils/smp/smp.cpp` (17) — contain **zero**
  `IDirect3D*` types, so neither lint nor a waf build (they are not in
  `wscript`) will ever read them. Stage 8 therefore takes a supplementary
  sweep — `git grep -I -E 'IDirect3D[A-Za-z]*9|D3DFMT_|D3DRS_|D3DTSS_'`
  over tracked files — and records its result alongside the two lint ids,
  so "the backend is gone" cannot be satisfied by pattern luck. The one
  `engine/` type hit is `engine/gl_rmain.cpp`.

## Gate 3 completion criteria

- `d3d9_com_types` = **0** and `dx_to_gl_abstraction` = **0**;
- `togl/`, `togles/`, `public/togl/`, `public/togles/`, `dx9sdk/`, and
  `stdshaders/*_dx6|dx7|dx8` (files + `.vpc` projects) deleted;
- boots on DX11 (Windows) — checklist 13 reports `D3D11`;
- Vulkan parity demonstrated through DXVK-Native, or the recorded
  deferral per decision 3;
- checklist 14 screenshots compared against the Stage 1 DX9 baseline;
- lint green with every other id unchanged, warning total at the
  `phase0.md §2` baseline, CI 6/6 green.

**Status: not started.** This file is the staged plan; execution notes
are appended per stage as they complete, as in `phase1a.md` / `phase2.md`.
