# Phase 3 Stage 2 - RHI surface audit (`IShader*` seam)

Stage 2 deliverable for the Stage-2 gate in `phase3.md` ("table covers
100% of methods; no code yet"). This document enumerates **every
method declaration** of the four interfaces the seam consists of, plus
the D3D9 enum surface inventoryed in the phase plan, and classifies
each one.

## How to read this table

| Verdict | Meaning |
| --- | --- |
| `direct` | The same concept exists in D3D11/DXGI; the backend implements it with a same-shaped API call or plain CPU bookkeeping. |
| `change` | The concept survives but must be re-expressed. The note says how (constant buffers, input layouts, PSO/state objects, sampler objects, render-target formats, device/swapchain, capability model). |
| `dead` | No D3D11 meaning. Deleted after the Stage-6 call-site audit (maintainer decision 4), or immediately when callers are already zero / the header itself says so. Some dead entries stay compile-time no-ops until Stage 7 retires their stdshaders call sites. |

Line references are to the headers as of this commit.
Verification method: `grep -cE '^\s*virtual'` per header (see
[Coverage](#coverage-and-how-to-re-verify)).

## Summary

| Interface | Header | direct | change | dead | total |
| --- | --- | ---: | ---: | ---: | ---: |
| `IShaderDeviceMgr` | `public/shaderapi/IShaderDevice.h:147` | 7 | 3 | 0 | **10** |
| `IShaderDevice` | `public/shaderapi/IShaderDevice.h:187` | 22 | 10 | 3 | **35** |
| `IShaderDynamicAPI` (base of `IShaderAPI`) | `public/shaderapi/ishaderdynamic.h:152` | 60 | 28 | 11 | **99** |
| `IShaderAPI` | `public/shaderapi/ishaderapi.h:138` | 106 | 65 | 8 | **179** |
| `IShaderShadow` | `public/shaderapi/ishadershadow.h:245` | 4 | 24 | 24 | **52** |
| **Total (gate surface)** | | **199** | **130** | **46** | **375** |

Related surface outside the gate's four interfaces: the `IAppSystem`
base inherited by `IShaderDeviceMgr` (5 methods, all `direct`,
unchanged), `IShaderBuffer` (3 methods, `direct` - it is an abstract
memory block, backend-agnostic), and the 6 non-virtual inline
`Create*Shader` helpers on `IShaderDevice` (not in the vtable; they
call `CompileShader` + the virtual `Create*Shader`).

---

## 1. `IShaderDeviceMgr` (10 methods)

Adapter discovery, mode setting, device creation.

| Method | Verdict | D3D11 note |
| --- | --- | --- |
| `GetAdapterCount` | direct | `IDXGIFactory::EnumAdapters1` count |
| `GetAdapterInfo` | change | `MaterialAdapterInfo_t::m_nDXLevel` re-specified as `D3D_FEATURE_LEVEL`; driver version comes from `DXGI_ADAPTER_DESC`/`DESC1` |
| `GetRecommendedConfigurationInfo` | change | D3DCAPS9-derived "recommended config" -> `CheckFeatureLevelSupport` + `CheckMultisampleQualityLevels` |
| `GetModeCount` | direct | `IDXGIOutput::GetDisplayModeList` |
| `GetModeInfo` | direct | `DXGI_MODE_DESC` -> `ShaderDisplayMode_t` |
| `GetCurrentModeInfo` | direct | `DXGI_OUTPUT_DESC` |
| `SetAdapter` | direct | Store index, create DXGI factory |
| `SetMode` | change | Device + swapchain creation (`D3D11CreateDeviceAndSwapChain`); `void* hWnd` becomes `HWND` / `SDL_Window*` (DXVK-Native WSI); 360-only `ShaderDeviceInfo_t` bits (`m_bProgressive`, hardware scaling) retire |
| `AddModeChangeCallback` | direct | Callback bookkeeping |
| `RemoveModeChangeCallback` | direct | Callback bookkeeping |
| *inherited:* `Connect`/`Disconnect`/`QueryInterface`/`Init`/`Shutdown` (`IAppSystem`, 5) | direct | Unchanged app-system lifecycle |

## 2. `IShaderDevice` (35 methods)

Swapchain/device control, shader and buffer creation.

| Method | Verdict | D3D11 note |
| --- | --- | --- |
| `ReleaseResources` | change | D3D9 default-pool release for device loss -> DXGI device-removed recovery; likely reduced to a no-op + recreate path (Stage 3 decides) |
| `ReacquireResources` | change | Pair of the above |
| `GetBackBufferFormat` | direct | Swapchain buffer `DXGI_FORMAT` -> `ImageFormat` via the Stage-5 format table |
| `GetBackBufferDimensions` | direct | `IDXGISwapChain::GetDesc` |
| `GetCurrentAdapter` | direct | Stored adapter index |
| `IsUsingGraphics` | direct | Bookkeeping |
| `SpewDriverInfo` | direct | `DXGI_ADAPTER_DESC` spew |
| `StencilBufferBits` | direct | Depth-stencil view format stencil bits |
| `IsAAEnabled` | direct | Swapchain `SampleDesc` |
| `Present` | direct | `IDXGISwapChain::Present` (flip model, vsync flag) |
| `GetWindowSize` | direct | Swapchain/window desc |
| `SetHardwareGammaRamp` | change | D3D9 `SetGammaRamp` -> DXGI fullscreen gamma ramp, windowed gamma becomes shader-side (Stage 5) |
| `AddView` | change | Child-window views -> additional swapchains (Hammer); game runtime stays single-view |
| `RemoveView` | change | As above |
| `SetView` | change | As above |
| `CompileShader` | change | `D3DXCompileShader*` -> `D3DCompile` (not linked anywhere today); profile strings retarget to `vs_4_0`/`ps_4_0` (Stage 4) |
| `CreateVertexShader` | direct | `ID3D11Device::CreateVertexShader`; handle wraps the COM pointer |
| `DestroyVertexShader` | direct | `Release` |
| `CreateGeometryShader` | direct | `ID3D11Device::CreateGeometryShader` (no stream-out needed at this seam) |
| `DestroyGeometryShader` | direct | `Release` |
| `CreatePixelShader` | direct | `ID3D11Device::CreatePixelShader` |
| `DestroyPixelShader` | direct | `Release` |
| `CreateStaticMesh` | direct | Header marks it deprecated but callers are live (`morph.cpp`, `movieobjects/`); `IMesh` remains an engine-level wrapper over VB/IB |
| `DestroyStaticMesh` | direct | As above |
| `CreateVertexBuffer` | direct | `D3D11_BUFFER_DESC`; usage-flag mapping is Stage 5 |
| `DestroyVertexBuffer` | direct | `Release` |
| `CreateIndexBuffer` | direct | As above |
| `DestroyIndexBuffer` | direct | `Release` |
| `GetDynamicVertexBuffer` | change | `bBuffered` ring-buffer discipline re-specified as `MAP_WRITE_DISCARD` rings (Stage 5, `dynamicvb.h`) |
| `GetDynamicIndexBuffer` | change | As above (`dynamicib.h`) |
| `EnableNonInteractiveMode` | dead | 360 loading-screen front-buffer path (header comment) |
| `RefreshFrontBufferNonInteractive` | dead | As above |
| `HandleThreadEvent` | change | Deferred `release/evict/reset/acquire` dispatch (`cmaterialsystem.cpp:3770`); managed-pool eviction dies, reset survives as state-cache rebuild |
| `DoStartupShaderPreloading` | dead | Compiled only under `DX_TO_GL_ABSTRACTION` (GL/toGL); dies with togl per the `dx_to_gl_abstraction` lint plan |
| `GetDisplayDeviceName` | direct | DXGI adapter name |

## 3. `IShaderDynamicAPI` (99 methods)

The dynamic-state base of `IShaderAPI`. Rows in header order.

| Method | Verdict | D3D11 note |
| --- | --- | --- |
| `SetViewports` | direct | `RSSetViewports` |
| `GetViewports` | direct | Cached viewport |
| `CurrentTime` | direct | CPU clock |
| `GetLightmapDimensions` | direct | Texture metadata |
| `GetSceneFogMode` | direct | CPU fog state, consumed by shader combo selection |
| `GetSceneFogColor` | direct | As above |
| `MatrixMode` | direct | CPU matrix stack (constant upload path moves to CBs, see C1) |
| `PushMatrix` | direct | As above |
| `PopMatrix` | direct | As Above |
| `LoadMatrix` | direct | As above |
| `MultMatrix` | direct | As above |
| `MultMatrixLocal` | direct | As above |
| `GetMatrix` | direct | As above |
| `LoadIdentity` | direct | As above |
| `LoadCameraToWorld` | direct | As above |
| `Ortho` | direct | As above |
| `PerspectiveX` | direct | As above |
| `PickMatrix` | direct | Hammer picking projection (CPU-side) |
| `Rotate` | direct | As above |
| `Translate` | direct | As above |
| `Scale` | direct | As above |
| `ScaleXY` | direct | As above |
| `Color3f` | dead | FFP constant color; impl writes `D3DRS_TEXTUREFACTOR` |
| `Color3fv` | dead | As above |
| `Color4f` | dead | As above |
| `Color4fv` | dead | As above |
| `Color3ub` | dead | As above |
| `Color3ubv` | dead | As above |
| `Color4ub` | dead | Verified: `shaderapidx8.cpp:4739` -> `SetSupportedRenderState(D3DRS_TEXTUREFACTOR, ...)` |
| `Color4ubv` | dead | As above |
| `SetVertexShaderConstant` | change | Flagship: register file `c0-c255` -> constant buffers with structured layout (C1) |
| `SetPixelShaderConstant` | change | As above |
| `SetDefaultState` | change | D3D9 dynamic default (incl. FFP bits) -> full D3D11 state-cache rebuild |
| `GetWorldSpaceCameraPosition` | direct | CPU value |
| `GetCurrentNumBones` | direct | CPU value |
| `GetCurrentLightCombo` | direct | Combo selection input |
| `GetCurrentFogType` | direct | Combo selection input |
| `SetTextureTransformDimension` | dead | D3D9 texture-transform stage (FFP) |
| `DisableTextureTransform` | dead | As above |
| `SetBumpEnvMatrix` | dead | `D3DBUMPENVMAP*` (FFP) |
| `SetVertexShaderIndex` | direct | Combo index -> preloaded shader |
| `SetPixelShaderIndex` | direct | As above |
| `GetBackBufferDimensions` | direct | Mirrors `IShaderDevice` |
| `GetMaxLights` | direct | Plain value (D3DCAPS source retires) |
| `GetLight` | direct | CPU light array (`LightDesc_t`) |
| `SetPixelShaderFogParams` | change | Fog constant upload (C1) |
| `SetVertexShaderStateAmbientLightCube` | change | Ambient cube -> CB (C1) |
| `SetPixelShaderStateAmbientLightCube` | change | As above |
| `CommitPixelShaderLighting` | change | Lighting constants -> CB (C1) |
| `GetVertexModifyBuilder` | direct | CPU mesh builder |
| `InFlashlightMode` | direct | CPU state |
| `GetFlashlightState` | direct | CPU state |
| `InEditorMode` | direct | CPU state |
| `GetBoundMorphFormat` | direct | Morph metadata |
| `BindStandardTexture` | direct | SRV bind of a standard texture |
| `GetRenderTargetEx` | direct | Handle lookup |
| `SetToneMappingScaleLinear` | change | Constant upload (C1) |
| `GetToneMappingScaleLinear` | direct | CPU readback |
| `GetLightMapScaleFactor` | direct | CPU value |
| `LoadBoneMatrix` | change | Bone matrices -> CB (C1) |
| `PerspectiveOffCenterX` | direct | Matrix stack |
| `SetFloatRenderingParameter` | direct | CPU renderparm slot (`renderparm.h`) |
| `SetIntRenderingParameter` | direct | As above |
| `SetVectorRenderingParameter` | direct | As above |
| `GetFloatRenderingParameter` | direct | As above |
| `GetIntRenderingParameter` | direct | As above |
| `GetVectorRenderingParameter` | direct | As above |
| `SetStencilEnable` | change | -> depth-stencil state object + `OMSetStencilRef` (C2); re-declared by `IShaderAPI` |
| `SetStencilFailOperation` | change | As above |
| `SetStencilZFailOperation` | change | As above |
| `SetStencilPassOperation` | change | As above |
| `SetStencilCompareFunction` | change | As above |
| `SetStencilReferenceValue` | change | Reference is dynamic state, not baked into the state object |
| `SetStencilTestMask` | change | As above |
| `SetStencilWriteMask` | change | As above |
| `ClearStencilBufferRectangle` | change | Per-rectangle clear under stencil state |
| `GetDXLevelDefaults` | change | Max/recommended DX level -> feature-level defaults; concept retires |
| `GetFlashlightStateEx` | direct | CPU state |
| `GetAmbientLightCubeLuminance` | direct | CPU value |
| `GetDX9LightState` | change | DX9 hardware-light query -> API-neutral `LightState_t` used only for combo selection |
| `GetPixelFogCombo` | direct | Combo selection input |
| `BindStandardVertexTexture` | direct | SRV (D3D11 has no vertex/pixel texture split) |
| `IsHWMorphingEnabled` | direct | CPU state |
| `GetStandardTextureDimensions` | direct | Texture metadata |
| `SetBooleanVertexShaderConstant` | change | D3D9 bool registers -> CB packing (C1) |
| `SetIntegerVertexShaderConstant` | change | As above |
| `SetBooleanPixelShaderConstant` | change | As above |
| `SetIntegerPixelShaderConstant` | change | As above |
| `ShouldWriteDepthToDestAlpha` | change | Depth-in-alpha trick -> separate depth copy / `SV_Depth` (Stage 5) |
| `PushDeformation` | direct | CPU deformation stack |
| `PopDeformation` | direct | As above |
| `GetNumActiveDeformations` | direct | As above |
| `GetPackedDeformationInformation` | direct | CPU packing; the caller's constant write is covered by `SetVertexShaderConstant` |
| `MarkUnusedVertexFields` | direct | Vertex-usage hints feeding input layouts (C3) |
| `ExecuteCommandBuffer` | change | Recorded command stream re-encoded for D3D11 semantics; entries that touch FFP/state-blocks die with Stage 6 |
| `SetStandardTextureHandle` | direct | Handle table |
| `GetCurrentColorCorrection` | direct | CPU state |
| `SetPSNearAndFarZ` | change | Constant upload (C1) |
| `SetDepthFeatheringPixelShaderConstant` | change | Constant upload (C1); re-declared by `IShaderAPI` |

## 4. `IShaderAPI` (179 declarations)

`IShaderAPI : IShaderDynamicAPI`. 161 are new; 18 re-declare base
methods (`SetViewports`, `GetViewports`, 6 rendering-parameter, 9
stencil, `SetDepthFeatheringPixelShaderConstant`) and share the base
row's verdict. Rows in header order.

| Method | Verdict | D3D11 note |
| --- | --- | --- |
| `SetViewports` | direct | Override; `RSSetViewports` |
| `GetViewports` | direct | Override |
| `ClearBuffers` | direct | `ClearRenderTargetView` / `ClearDepthStencilView` |
| `ClearColor3ub` | direct | Cached clear colour |
| `ClearColor4ub` | direct | As above |
| `BindVertexShader` | direct | `VSSetShader` |
| `BindGeometryShader` | direct | `GSSetShader` |
| `BindPixelShader` | direct | `PSSetShader` |
| `SetRasterState` | change | Rasterizer state object (C2) |
| `SetMode` | change | Device + swapchain; shared re-spec with `IShaderDeviceMgr::SetMode` |
| `ChangeVideoMode` | change | `ResizeBuffers` / fullscreen transition |
| `TakeSnapshot` | change | Snapshot no longer encodes FFP texture-stage state; becomes a key over shader combo + state-object inputs |
| `TexMinFilter` | change | Per-texture sampler state -> sampler objects (C4) |
| `TexMagFilter` | change | As above |
| `TexWrap` | change | As above |
| `CopyRenderTargetToTexture` | change | `GetRenderTargetData` -> `CopyResource`/staging (+ MSAA resolve) |
| `Bind` | direct | Material bookkeeping |
| `FlushBufferedPrimitives` | direct | Mesh flush |
| `GetDynamicMesh` | direct | Mesh system (VB ring discipline: Stage 5) |
| `GetDynamicMeshEx` | direct | As above |
| `IsTranslucent` | direct | Snapshot metadata query |
| `IsAlphaTested` | direct | As above |
| `UsesVertexAndPixelShaders` | direct | As above |
| `IsDepthWriteEnabled` | direct | As above |
| `ComputeVertexFormat` | direct | Format math (layout creation in C3) |
| `ComputeVertexUsage` | direct | As above |
| `BeginPass` | change | Per-pass binding: PSO + SRV/CBV/RTV instead of a D3D9 state cascade |
| `RenderPass` | change | Draw with bound PSO |
| `SetNumBoneWeights` | direct | CPU constant-upload sizing |
| `SetLight` | direct | CPU light array |
| `SetLightingOrigin` | direct | CPU value |
| `SetAmbientLight` | direct | CPU value |
| `SetAmbientLightCube` | direct | Stores the cube; upload rows are `Set*StateAmbientLightCube` |
| `ShadeMode` | dead | Gouraud/Flat; FFP only |
| `CullMode` | change | Rasterizer state (C2) |
| `ForceDepthFuncEquals` | change | Depth-stencil state (C2) |
| `OverrideDepthEnable` | change | As above |
| `SetHeightClipZ` | direct | User clip plane / clip distance |
| `SetHeightClipMode` | change | `ZRANGE`-fade mode has no DX11 equivalent; clip-only or shader-side (Stage 6 audit) |
| `SetClipPlane` | direct | Clip planes 0-7 |
| `EnableClipPlane` | direct | As above |
| `SetSkinningMatrices` | change | Bone matrix CB upload (C1) |
| `GetNearestSupportedFormat` | change | DXGI format negotiation (C5) |
| `GetNearestRenderTargetFormat` | change | As above |
| `DoRenderTargetsNeedSeparateDepthBuffer` | direct | Always separate in D3D11 |
| `CreateTexture` | direct | `CreateTexture2D`; `TEXTURE_CREATE_*` flags remapped + format table (C5) |
| `DeleteTexture` | direct | `Release` |
| `CreateDepthTexture` | direct | DSV (+ SRV) pair creation |
| `IsTexture` | direct | Handle validity |
| `IsTextureResident` | dead | D3D9 managed-pool residency query |
| `ModifyTexture` | direct | Bind-for-update bookkeeping |
| `TexImage2D` | direct | `UpdateSubresource`; `bSrcIsTiled` / 360 branches deleted |
| `TexSubImage2D` | direct | `UpdateSubresource`; as above |
| `TexImageFromVTF` | direct | Decode + upload (sRGB flag via C5) |
| `TexLock` | change | CPU writes -> `Map`/staging (Stage 5) |
| `TexUnlock` | change | As above |
| `TexSetPriority` | dead | D3D9 resource priority |
| `BindTexture` | direct | SRV + sampler slot |
| `SetRenderTarget` | change | `OMSetRenderTargets`; `SHADER_RENDERTARGET_*` sentinels -> swapchain views (C5) |
| `ClearBuffersObeyStencil` | change | Stencil-obeyed clear -> per-rectangle clears / stencil state |
| `ReadPixels` (x,y,w,h) | change | `GetRenderTargetData` -> staging copy with row pitch + format conversion |
| `ReadPixels` (Rect_t) | change | As above |
| `FlushHardware` | direct | `Flush` |
| `BeginFrame` | direct | Frame bracket |
| `EndFrame` | direct | As above |
| `SelectionMode` | direct | Verified CPU-side selection buffer (`shaderapidx8.cpp:11575`); Hammer picking keeps working |
| `SelectionBuffer` | direct | As above |
| `ClearSelectionNames` | direct | As above |
| `LoadSelectionName` | direct | As above |
| `PushSelectionName` | direct | As above |
| `PopSelectionName` | direct | As above |
| `ForceHardwareSync` | change | D3D9 event/query -> `Flush` or `ID3D11Query` event |
| `ClearSnapshots` | direct | Snapshot bookkeeping |
| `FogStart` | change | `D3DRS_FOG*` -> shader-side fog (values -> C1) |
| `FogEnd` | change | As above |
| `SetFogZ` | change | As above |
| `SceneFogColor3ub` | change | As above |
| `GetSceneFogColor` | direct | CPU readback |
| `SceneFogMode` | change | FFP fog mode -> combo/constant input only |
| `CanDownloadTextures` | direct | Always-true bookkeeping |
| `ResetRenderState` | change | D3D9 state-block save/restore dies -> full D3D11 state-cache rebuild |
| `GetCurrentDynamicVBSize` | direct | Mesh bookkeeping |
| `DestroyVertexBuffers` | direct | Mesh bookkeeping |
| `EvictManagedResources` | dead | Managed pool |
| `SetAnisotropicLevel` | change | Global sampler state -> sampler objects (C4) |
| `SyncToken` | direct | Token bookkeeping |
| `SetStandardVertexShaderConstants` | change | CB upload (C1) |
| `CreateOcclusionQueryObject` | direct | `ID3D11Query` with `D3D11_QUERY_OCCLUSION` |
| `DestroyOcclusionQueryObject` | direct | `Release` |
| `BeginOcclusionQueryDrawing` | direct | `Begin` |
| `EndOcclusionQueryDrawing` | direct | `End` |
| `OcclusionQuery_GetNumPixelsRendered` | direct | `GetData` + `D3D11_ASYNC_GETDATA_FLUSH` |
| `SetFlashlightState` | direct | CPU state -> combo/constants |
| `ClearVertexAndPixelShaderRefCounts` | direct | Shader cache maintenance |
| `PurgeUnusedVertexAndPixelShaders` | direct | Shader cache maintenance |
| `DXSupportLevelChanged` | change | DX-level concept -> feature level (re-specified) |
| `EnableUserClipTransformOverride` | direct | Clip transform bookkeeping |
| `UserClipTransform` | direct | As above |
| `ComputeMorphFormat` | direct | Morph metadata |
| `SetRenderTargetEx` | change | Indexed render targets -> RTV arrays (C5) |
| `CopyRenderTargetToTextureEx` | change | Staging copy with rects |
| `CopyTextureToRenderTargetEx` | change | As above |
| `HandleDeviceLost` | change | D3D9 lost-device -> DXGI device-removed recovery (Stage 3) |
| `EnableLinearColorSpaceFrameBuffer` | change | sRGB render-target formats (C5) |
| `SetFullScreenTextureHandle` | direct | Handle bookkeeping |
| `SetFloatRenderingParameter` | direct | Override (base row above) |
| `SetIntRenderingParameter` | direct | Override |
| `SetVectorRenderingParameter` | direct | Override |
| `GetFloatRenderingParameter` | direct | Override |
| `GetIntRenderingParameter` | direct | Override |
| `GetVectorRenderingParameter` | direct | Override |
| `SetFastClipPlane` | direct | Clip plane |
| `EnableFastClip` | direct | As above |
| `GetMaxToRender` | direct | Mesh capacity query |
| `GetMaxVerticesToRender` | direct | As above |
| `GetMaxIndicesToRender` | direct | As above |
| `SetStencilEnable` | change | Override; C2 |
| `SetStencilFailOperation` | change | Override; C2 |
| `SetStencilZFailOperation` | change | Override; C2 |
| `SetStencilPassOperation` | change | Override; C2 |
| `SetStencilCompareFunction` | change | Override; C2 |
| `SetStencilReferenceValue` | change | Override; C2 |
| `SetStencilTestMask` | change | Override; C2 |
| `SetStencilWriteMask` | change | Override; C2 |
| `ClearStencilBufferRectangle` | change | Override; C2 |
| `DisableAllLocalLights` | direct | CPU light array |
| `CompareSnapshots` | direct | Snapshot comparison |
| `GetFlexMesh` | direct | Mesh handle |
| `SetFlashlightStateEx` | direct | CPU state |
| `SupportsMSAAMode` | change | D3DCAPS nine-level MSAA linearity -> `CheckMultisampleQualityLevels` |
| `OwnGPUResources` | direct | Resource ownership flag |
| `GetFogDistances` | direct | CPU readback |
| `BeginPIXEvent` | direct | `ID3DUserDefinedAnnotation` |
| `EndPIXEvent` | direct | As above |
| `SetPIXMarker` | direct | As above |
| `EnableAlphaToCoverage` | direct | Blend-state A2C |
| `DisableAlphaToCoverage` | direct | As above |
| `ComputeVertexDescription` | direct | `MeshDesc_t` offsets (feeds C3) |
| `SupportsShadowDepthTextures` | direct | Feature query |
| `SetDisallowAccess` | direct | Thread bookkeeping |
| `EnableShaderShaderMutex` | direct | As above |
| `ShaderLock` | direct | As above |
| `ShaderUnlock` | direct | As above |
| `GetShadowDepthTextureFormat` | direct | Format table (C5) |
| `SupportsFetch4` | dead | ATI4 packed-vertex-fetch trick, D3D9-era |
| `SetShadowDepthBiasFactors` | change | D3D9 bias units/slope -> `DepthBias`/`DepthBiasClamp` semantics |
| `BindVertexBuffer` | change | Input layout create/cache + `IASetVertexBuffers` (C3) |
| `BindIndexBuffer` | change | `IASetIndexBuffer` |
| `Draw` | change | `MaterialPrimitiveType_t` -> `IASetPrimitiveTopology` + `DrawIndexed` |
| `PerformFullScreenStencilOperation` | change | Full-screen stencil pass under PSO (C2) |
| `SetScissorRect` | direct | `RSSetScissorRects` |
| `SupportsCSAAMode` | dead | NVIDIA CSAA sample patterns are D3D9-only |
| `InvalidateDelayedShaderConstants` | change | Delayed-constant flush -> immediate CB updates; call retires to a no-op |
| `GammaToLinear_HardwareSpecific` | direct | CPU math |
| `LinearToGamma_HardwareSpecific` | direct | CPU math |
| `SetLinearToGammaConversionTextures` | change | LUT textures -> `_SRGB` formats / shader-side conversion (C5) |
| `GetNullTextureFormat` | direct | Format table (C5) |
| `BindVertexTexture` | direct | SRV |
| `EnableHWMorphing` | direct | Morph SRV / combo selection |
| `SetFlexWeights` | direct | Dynamic VB write (Stage 5 discard) |
| `FogMaxDensity` | change | Fog constant (C1) |
| `CreateTextures` | direct | Batched `CreateTexture2D` |
| `AcquireThreadOwnership` | direct | Thread bookkeeping |
| `ReleaseThreadOwnership` | direct | As above |
| `SupportsNormalMapCompression` | dead | Header body: "This has all been removed." (asserts) |
| `EnableBuffer2FramesAhead` | dead | Header comment: Xbox 360 only |
| `SetDepthFeatheringPixelShaderConstant` | change | Override; CB (C1) |
| `PrintfVA` | direct | Debug spew |
| `Printf` | direct | As above |
| `Knob` | direct | Debug knob |
| `OverrideAlphaWriteEnable` | change | Render-target write mask -> blend state (C2) |
| `OverrideColorWriteEnable` | change | As above |
| `ClearBuffersObeyStencilEx` | change | Per-rectangle/stencil clear |
| `CopyRenderTargetToScratchTexture` | change | Staging copy |
| `LockRect` | change | `Map`/staging (Stage 5) |
| `UnlockRect` | change | As above |
| `TexLodClamp` | change | Sampler `MinLOD` (C4) |
| `TexLodBias` | change | Sampler LOD bias (C4) |
| `CopyTextureToTexture` | direct | `CopyResource` |

## 5. `IShaderShadow` (52 methods)

Shadow (static) state. Highest dead density: this is where the FFP
combiner vocabulary lives.

| Method | Verdict | D3D11 note |
| --- | --- | --- |
| `SetDefaultState` | change | D3D9 default (incl. FFP) -> state-object defaults (C2) |
| `DepthFunc` | change | Depth-stencil state (C2) |
| `EnableDepthWrites` | change | As above |
| `EnableDepthTest` | change | As above |
| `EnablePolyOffset` | change | Rasterizer `DepthBias` (C2) |
| `EnableStencil` | dead | Header: obsolete stub; stencil is owned by `IShaderAPI`/material system |
| `StencilFunc` | dead | As above |
| `StencilPassOp` | dead | As above |
| `StencilFailOp` | dead | As above |
| `StencilDepthFailOp` | dead | As above |
| `StencilReference` | dead | As above |
| `StencilMask` | dead | As above |
| `StencilWriteMask` | dead | As above |
| `EnableColorWrites` | change | Render-target write mask (C2) |
| `EnableAlphaWrites` | change | As above |
| `EnableBlending` | change | Blend state (C2) |
| `BlendFunc` | change | As above |
| `EnableAlphaTest` | change | FFP alpha test -> `clip()` in the pixel shader (or A2C) |
| `AlphaFunc` | change | As above |
| `PolyMode` | change | Rasterizer fill mode (C2) |
| `EnableCulling` | change | Rasterizer cull mode (C2) |
| `EnableConstantColor` | dead | `D3DTA_CONSTANT` FFP combiner argument |
| `VertexShaderVertexFormat` | change | -> input-layout description (C3) |
| `SetVertexShader` | direct | Combo index -> preloaded shader; profile retarget is Stage 4 |
| `SetPixelShader` | direct | As above |
| `EnableLighting` | dead | FFP lighting; **zero callers** (verified by grep) |
| `EnableSpecular` | dead | **Zero callers** (verified by grep) |
| `EnableSRGBWrite` | change | `_SRGB` RTV format (C5) |
| `EnableSRGBRead` | change | `_SRGB` SRV (C5) |
| `EnableVertexBlend` | dead | FFP vertex blending; zero callers |
| `OverbrightValue` | dead | Texture-stage modulate; callers are `*_dx6.cpp` legacy shaders + `BaseShader` fallbacks (Stage 6) |
| `EnableTexture` | change | **Live** (water, worldtwotextureblend, vortwarp, ...); sampler declaration -> SRV/sampler slot state, kept until Stage 7 |
| `EnableTexGen` | dead | FFP texgen; only `BaseShader` legacy paths call it |
| `TexGen` | dead | As above |
| `EnableCustomPixelPipe` | dead | DX5-7 combiner pipeline |
| `CustomTextureStages` | dead | As above |
| `CustomTextureOperation` | dead | As above |
| `DrawFlags` | dead | FFP vertex-draw mask; callers: `wireframe.cpp` + `*_dx6.cpp` (converted in Stage 7) |
| `EnableAlphaPipe` | dead | FFP alpha pipeline |
| `EnableConstantAlpha` | dead | As above |
| `EnableVertexAlpha` | dead | As above |
| `EnableTextureAlpha` | dead | As above |
| `EnableBlendingSeparateAlpha` | change | Independent blend factors (C2) |
| `BlendFuncSeparateAlpha` | change | As above |
| `FogMode` | change | FFP fog -> shader fog (C1) |
| `SetDiffuseMaterialSource` | dead | `D3DMATERIAL9` source; zero callers |
| `SetMorphFormat` | direct | Morph metadata |
| `DisableFogGammaCorrection` | change | Fog becomes shader-side; flag re-specified or dropped (Stage 6) |
| `EnableAlphaToCoverage` | direct | A2C flag inside blend state |
| `SetShadowDepthFiltering` | change | Sampler object (C4) |
| `BlendOp` | change | Blend state (C2) |
| `BlendOpSeparateAlpha` | change | As above |

## 6. D3D9 enum surface (inventory from `phase3.md`)

Repo-wide reference counts from the phase plan's inventory table. These
are not interface methods but they are the values flowing through the
method arguments above; each family gets the same three verdicts.

| Family (references) | Verdict | D3D11 mapping |
| --- | --- | --- |
| `D3DRS_*` (1335) | change | Render states become rasterizer / depth-stencil / blend state objects (C2); the FFP subset (lighting, fog, texture factor, transform enable) is `dead` and dies with Stage 6 |
| `D3DFMT_*` (785) | change | One central `ImageFormat` <-> `DXGI_FORMAT` table (C5); negotiation via `CheckFeatureLevelSupport` |
| `D3DCMP_*` / `D3DBLEND_*` (258) | change | `D3D11_COMPARISON_*` / `D3D11_BLEND_*` values inside state-object descriptions |
| `D3DTSS_*` (187) | dead | Texture-stage state (combiners) has no D3D11 meaning; the filter/wrap subset lives on as `D3DSAMP_*` -> sampler objects |
| `D3DLIGHT*` / `D3DMATERIAL9` (169) | dead | FFP lighting; replaced by CPU-side `LightDesc_t` feeding shader constants (C1) |
| `D3DSAMP_*` (129) | change | D3D11 sampler objects (C4) |
| `D3DPT_*` (79) | direct | `IASetPrimitiveTopology`; the topology values align 1:1 |
| `D3DTS_*` (77) | dead | Transform stages; CPU matrix stack + constant buffers own this already |
| `IDirect3D*9` COM types (924, lint id `d3d9_com_types`) | dead | Die with `shaderapidx9`; the replacement project keeps the lint row at 0 by construction |

## 7. Cross-cutting re-specifications

The `change` rows do not each need an independent design; they collapse
into these mechanisms, specified once (maintainer decision 2):

* **C1 - Constant buffers.** All 40-odd "write a constant register"
  rows (`SetVertexShaderConstant`, ambient cube, lighting, fog,
  bones/skinning, tonemap, near/far Z, depth feathering, bool/int
  constants) become one structured CB layout (per-frame / per-draw /
  bone matrices) updated with `UpdateSubresource` or `Map`. Register
  indices survive as byte offsets; `bForce`/delayed-constant
  bookkeeping disappears with
  `InvalidateDelayedShaderConstants`.
* **C2 - State objects (PSO-lite).** D3D9 allowed render state to be
  set at any time; D3D11 binds state objects. The backend caches
  rasterizer / depth-stencil / blend objects keyed on the tuple the
  `change` rows produce, and binds them at `BeginPass`/`RenderPass`.
  Dynamic pieces (stencil ref, stencil masks, viewport, scissor) stay
  dynamic.
* **C3 - Input layouts.** `VertexFormat_t` + `VertexShaderVertexFormat`
  + `ComputeVertexDescription` + `MarkUnusedVertexFields` describe the
  vertex input; the backend creates and caches
  `ID3D11InputLayout` objects keyed by (`VertexFormat_t`, VS bytecode)
  at `BindVertexBuffer`.
* **C4 - Sampler objects.** `TexMin/Mag/Wrap`, `SetAnisotropicLevel`,
  `TexLodBias`, `TexLodClamp`, `SetShadowDepthFiltering` become
  `ID3D11SamplerState` objects keyed by the state tuple, bound per
  slot. Per-texture mutable sampler state (the D3D9 model) ends.
* **C5 - Format table.** One `ImageFormat` <-> `DXGI_FORMAT` table
  owns every `D3DFMT_*` / render-target / sRGB / negotiation
  conversion (`GetNearestSupportedFormat`, `_SRGB` rows,
  `SetRenderTarget*` sentinels, readback conversions).
* **C6 - Dead staging.** The 46 `dead` rows are removed in Stage 6
  after the call-site audit (maintainer decision 4). Evidence already
  collected: zero callers (`EnableLighting`, `EnableSpecular`,
  `EnableVertexBlend`, `SetDiffuseMaterialSource`), explicit stubs
  (8 shadow stencil methods), header-declared
  (`SupportsNormalMapCompression`, `EnableBuffer2FramesAhead`, 360
  paths), FFP-only with verified impl (`Color*` -> `D3DRS_TEXTUREFACTOR`),
  and shader-side legacy (`CustomTextureOperation`, `TexGen`,
  `DrawFlags`, `OverbrightValue` - callers are `*_dx6.cpp` +
  `BaseShader` fallbacks, retired with Stage 7's stdshaders work).
* **C7 - Prior art.** `materialsystem/shaderapidx9/shaderapidx10.cpp`
  and `shaderdevicedx10.cpp` are an unfinished D3D10 backend
  (`shaderapidx10.vpc`, registered in `vpc_scripts/projects.vgc` but
  **not built by waf** - no artifacts). The header comments "these
  methods have been ported to DX10" refer to it. Stage 3's
  device/swapchain spike should crib from it, keeping in mind it
  predates the flip-model swapchain and still mixes D3D9 idioms.

## Open questions carried into Stage 3

* `ReleaseResources`/`ReacquireResources`/`HandleDeviceLost`/
  `HandleThreadEvent`'s evict event: reduce to a device-removed
  recovery path, or no-op? Decided when the device is created (Stage 3).
* `AddView`/`RemoveView`/`SetView` (multi-window) must stay alive for
  Hammer even if the game runtime is single-swapchain.
* Selection mode stays CPU-side (verified) - no work, just don't break
  it.
* Geometry shaders: seam supports them; nothing in the stock shader
  set needs stream-out. Keep the row, don't build infrastructure for it.

## Coverage and how to re-verify

The gate is "table covers 100% of methods". Recount with:

```
grep -cE '^\s*virtual' public/shaderapi/IShaderDevice.h   # 51 = 10 mgr + 35 device + 3 IShaderBuffer + 3 CUtlShaderBuffer
grep -cE '^\s*virtual' public/shaderapi/ishaderdynamic.h  # 99  = IShaderDynamicAPI
grep -cE '^\s*virtual' public/shaderapi/ishaderapi.h      # 179 = IShaderAPI
grep -cE '^\s*virtual' public/shaderapi/ishadershadow.h   # 52  = IShaderShadow
```

Gate surface = 10 + 35 + 99 + 179 + 52 = **375 methods**, every one
appearing as exactly one row above (overrides are marked). Verdict
totals: **199 direct / 130 change / 46 dead**.

Stage 2 changes no code - this document plus the Stage 1 captures are
the only artefacts.
