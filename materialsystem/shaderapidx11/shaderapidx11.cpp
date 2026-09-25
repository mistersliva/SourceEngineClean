//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: D3D11 shader backend - Phase 3 Stage 3 (device + swapchain spike).
//
//          Behind the existing IShader* seam:
//            - This file provides IShaderDeviceMgr + IShaderDevice for real
//              (DXGI adapter/mode enumeration, D3D11 device + swapchain,
//              clear, present, resize/alt-tab/vid_restart re-entry).
//            - The rest of the seam (IShaderAPI, IShaderShadow,
//              IHardwareConfigInternal) is the shared stub implementation in
//              shaderapiempty.cpp, compiled into this DLL with
//              SHADERAPIEMPTY_BACKEND_BUILD and routed here through
//              shaderapi_backend_hooks.h for SetMode/ClearBuffers.
//
//          Stage 3 renders one known clear colour (see SPIKE_CLEAR_COLOR)
//          through the real materialsystem frame path, so the swapchain
//          pipeline is verifiable before shaders (Stage 4), resources
//          (Stage 5) and draws land. Select with "-dx11"
//          (launcher.cpp / VguiMatSysApp.cpp); the DX9 build stays the
//          default.
//
//          Known stub-seam gap, fixed in Stage 5: g_pShaderAPI->
//          GetBackBufferDimensions() still forwards to the empty device's
//          fixed 1024x768 because the stub API class predates this device.
//          Harmless until render targets exist (Stage 3 draws land on
//          CEmptyMesh placeholders and rasterize nothing).
//
//===========================================================================//

#include "tier1/interface.h"
#include "tier1/utlvector.h"
#include "tier1/strtools.h"
#include "tier0/dbg.h"
#include "materialsystem/imaterialsystem.h"
#include "shaderapi/IShaderDevice.h"
#include "shaderapi/ishaderapi.h"
#include "shaderapi/ishadershadow.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi_backend_hooks.h"
#include "emptymesh.h"		// CEmptyMesh placeholders handed out below

#include <d3d11.h>
#if defined( __has_include )
#	if __has_include( <dxgi1_1.h> )
#		include <dxgi1_1.h>	// classic SDK layout: factory/adapter1 live here
#	else
#		include <dxgi.h>		// SDK 10.0.26100+ folded dxgi1_1 into shared/dxgi.h
#	endif
#else
#	include <dxgi.h>
#endif

// Provided by shaderapiempty.cpp (compiled into this DLL).
extern IShaderUtil *g_pShaderUtil;


//-----------------------------------------------------------------------------
// The Stage-3 spike clear colour: a fixed, unmistakable magenta so a running
// swapchain is obvious on screen. Stage 5 starts honouring the colour the
// engine asks for and drops the present-time safety clear.
//-----------------------------------------------------------------------------
static const float s_SpikeClearColor[4] = { 1.0f, 0.0f, 1.0f, 1.0f };


//-----------------------------------------------------------------------------
// The two real objects of this DLL
//-----------------------------------------------------------------------------

struct Dx11AdapterInfo_t
{
	MaterialAdapterInfo_t m_Info;
	IDXGIAdapter1 *m_pAdapter;
};


//-----------------------------------------------------------------------------
// DXGI adapter/mode enumeration + device manager lifecycle
//-----------------------------------------------------------------------------
class CShaderDeviceMgrDx11 : public IShaderDeviceMgr
{
public:
	CShaderDeviceMgrDx11();

	// IAppSystem
	virtual bool Connect( CreateInterfaceFn factory );
	virtual void Disconnect();
	virtual void *QueryInterface( const char *pInterfaceName );
	virtual InitReturnVal_t Init();
	virtual void Shutdown();

	// IShaderDeviceMgr
	virtual int GetAdapterCount() const;
	virtual void GetAdapterInfo( int adapter, MaterialAdapterInfo_t &info ) const;
	virtual bool GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pKeyValues );
	virtual int GetModeCount( int adapter ) const;
	virtual void GetModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter, int mode ) const;
	virtual void GetCurrentModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter ) const;
	virtual bool SetAdapter( int nAdapter, int nFlags );
	virtual CreateInterfaceFn SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode );
	virtual void AddModeChangeCallback( ShaderModeChangeCallbackFunc_t func );
	virtual void RemoveModeChangeCallback( ShaderModeChangeCallbackFunc_t func );

	// Used by the device half of this DLL
	IDXGIFactory1 *GetFactory() const { return m_pFactory; }
	IDXGIAdapter1 *GetAdapter( int nAdapter ) const;
	void NotifyModeChange();

private:
	void EnumerateModes( int nAdapter, CUtlVector<DXGI_MODE_DESC> &modes ) const;

	IDXGIFactory1 *m_pFactory;
	CUtlVector<Dx11AdapterInfo_t> m_Adapters;
	CUtlVector<ShaderModeChangeCallbackFunc_t> m_ModeChangeCallbacks;
	int m_nAdapter;
};


//-----------------------------------------------------------------------------
// The D3D11 device + swapchain
//-----------------------------------------------------------------------------
class CShaderDeviceDx11 : public IShaderDevice
{
public:
	CShaderDeviceDx11();

	// Creates (or recreates, for vid_restart / mode change) device + swapchain.
	bool CreateDevice( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode );
	void DestroyDevice();

	// Clears the backbuffer to the spike colour (see above).
	void ClearToSpikeColor();

	// IShaderDevice
	virtual void ReleaseResources();
	virtual void ReacquireResources();
	virtual ImageFormat GetBackBufferFormat() const;
	virtual void GetBackBufferDimensions( int &width, int &height ) const;
	virtual int GetCurrentAdapter() const;
	virtual bool IsUsingGraphics() const;
	virtual void SpewDriverInfo() const;
	virtual int StencilBufferBits() const;
	virtual bool IsAAEnabled() const;
	virtual void Present();
	virtual void GetWindowSize( int &nWidth, int &nHeight ) const;
	virtual void SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled );
	virtual bool AddView( void *hWnd );
	virtual void RemoveView( void *hWnd );
	virtual void SetView( void *hWnd );
	virtual IShaderBuffer *CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion );
	virtual VertexShaderHandle_t CreateVertexShader( IShaderBuffer *pShaderBuffer );
	virtual void DestroyVertexShader( VertexShaderHandle_t hShader );
	virtual GeometryShaderHandle_t CreateGeometryShader( IShaderBuffer *pShaderBuffer );
	virtual void DestroyGeometryShader( GeometryShaderHandle_t hShader );
	virtual PixelShaderHandle_t CreatePixelShader( IShaderBuffer *pShaderBuffer );
	virtual void DestroyPixelShader( PixelShaderHandle_t hShader );
	virtual IMesh *CreateStaticMesh( VertexFormat_t vertexFormat, const char *pTextureBudgetGroup, IMaterial *pMaterial );
	virtual void DestroyStaticMesh( IMesh *mesh );
	virtual IVertexBuffer *CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup );
	virtual void DestroyVertexBuffer( IVertexBuffer *pVertexBuffer );
	virtual IIndexBuffer *CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup );
	virtual void DestroyIndexBuffer( IIndexBuffer *pIndexBuffer );
	virtual IVertexBuffer *GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered );
	virtual IIndexBuffer *GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered );
	virtual void EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo );
	virtual void RefreshFrontBufferNonInteractive();
	virtual void HandleThreadEvent( uint32 threadEvent );
	virtual char *GetDisplayDeviceName();

private:
	bool CreateBackBufferView();
	bool CheckAndResize();			// handles window resize + alt-tab restore

	HWND m_hWnd;
	ID3D11Device *m_pDevice;
	ID3D11DeviceContext *m_pContext;
	IDXGISwapChain *m_pSwapChain;
	ID3D11RenderTargetView *m_pRTV;
	D3D_FEATURE_LEVEL m_FeatureLevel;
	int m_nAdapter;
	int m_nBackBufferCount;
	int m_nWidth;
	int m_nHeight;
	bool m_bDeviceReady;
	bool m_bPresentedSinceClear;
	bool m_bWaitForVSync;
	char m_szDisplayDeviceName[256];

	// Placeholder meshes handed to callers of the IShaderDevice buffer
	// factories. studiorender dereferences CreateStaticMesh's result
	// (R_StudioBuildMeshGroup) while loading static props at menu boot, and
	// the empty implementation always returns these - Stage 5 replaces them
	// with real D3D11 buffers.
	CEmptyMesh m_Mesh;
	CEmptyMesh m_DynamicMesh;
};


//-----------------------------------------------------------------------------
// Backend hooks: route the stub seam's SetMode/ClearBuffers into the device
//-----------------------------------------------------------------------------
class CShaderAPIBackendHooksDx11 : public IShaderAPIBackendHooks
{
public:
	virtual bool SetMode( void *hwnd, int nAdapter, const ShaderDeviceInfo_t &info );
	virtual void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight );
	virtual bool CanDownloadTextures() const;
};

static CShaderAPIBackendHooksDx11 g_BackendHooksDx11;


//-----------------------------------------------------------------------------
// Interface exposure: only the device + device manager come from here. The
// stub API/shadow/hardware-config exposures are compiled out of
// shaderapiempty.cpp by SHADERAPIEMPTY_BACKEND_BUILD for the device/device
// manager side, and kept for everything else.
//-----------------------------------------------------------------------------
static CShaderDeviceMgrDx11 s_ShaderDeviceMgrDx11;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceMgrDx11, IShaderDeviceMgr,
								  SHADER_DEVICE_MGR_INTERFACE_VERSION, s_ShaderDeviceMgrDx11 )

static CShaderDeviceDx11 s_ShaderDeviceDx11;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceDx11, IShaderDevice,
								  SHADER_DEVICE_INTERFACE_VERSION, s_ShaderDeviceDx11 )

static void *ShaderDeviceDx11InterfaceFactory( const char *pInterfaceName, int *pReturnCode )
{
	if ( pReturnCode )
		*pReturnCode = IFACE_OK;
	if ( !Q_stricmp( pInterfaceName, SHADER_DEVICE_INTERFACE_VERSION ) )
		return static_cast<IShaderDevice *>( &s_ShaderDeviceDx11 );
	if ( pReturnCode )
		*pReturnCode = IFACE_FAILED;
	return NULL;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
// CShaderDeviceMgrDx11
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShaderDeviceMgrDx11::CShaderDeviceMgrDx11()
{
	m_pFactory = NULL;
	m_nAdapter = 0;
}

bool CShaderDeviceMgrDx11::Connect( CreateInterfaceFn factory )
{
	// Same pattern as the empty implementation: pull IShaderUtil (the
	// material system) through the factory materialsystem handed us.
	g_pShaderUtil = (IShaderUtil *)factory( SHADER_UTIL_INTERFACE_VERSION, NULL );

	// Route the stub seam's SetMode/ClearBuffers to the real device. This is
	// the point where this DLL takes ownership of device-level calls.
	g_pShaderAPIBackendHooks = &g_BackendHooksDx11;
	return true;
}

void CShaderDeviceMgrDx11::Disconnect()
{
	g_pShaderAPIBackendHooks = NULL;
	g_pShaderUtil = NULL;
}

void *CShaderDeviceMgrDx11::QueryInterface( const char *pInterfaceName )
{
	if ( !Q_stricmp( pInterfaceName, SHADER_DEVICE_MGR_INTERFACE_VERSION ) )
		return static_cast<IShaderDeviceMgr *>( this );
	return NULL;
}

InitReturnVal_t CShaderDeviceMgrDx11::Init()
{
	HRESULT hr = CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), reinterpret_cast<void **>( &m_pFactory ) );
	if ( FAILED( hr ) || !m_pFactory )
	{
		Warning( "shaderapidx11: CreateDXGIFactory1 failed (0x%08x)\n", (unsigned int)hr );
		return INIT_FAILED;
	}

	for ( UINT i = 0; ; ++i )
	{
		IDXGIAdapter1 *pAdapter = NULL;
		if ( FAILED( m_pFactory->EnumAdapters1( i, &pAdapter ) ) || !pAdapter )
			break;

		Dx11AdapterInfo_t entry;
		memset( &entry.m_Info, 0, sizeof( entry.m_Info ) );
		entry.m_pAdapter = pAdapter;

		DXGI_ADAPTER_DESC1 desc;
		if ( SUCCEEDED( pAdapter->GetDesc1( &desc ) ) )
		{
			WideCharToMultiByte( CP_ACP, 0, desc.Description, -1,
				entry.m_Info.m_pDriverName, MATERIAL_ADAPTER_NAME_LENGTH, NULL, NULL );
			entry.m_Info.m_VendorID = desc.VendorId;
			entry.m_Info.m_DeviceID = desc.DeviceId;
		}
		// This backend speaks D3D11: advertise feature-level 11.0-class
		// support instead of the DX9-era m_nDXSupportLevel ladder. The
		// concept itself retires with the format/capability work in Stage 5
		// (phase3_rhi_map.md: GetAdapterInfo / GetDXLevelDefaults).
		entry.m_Info.m_nDXSupportLevel = 110;
		entry.m_Info.m_nMaxDXSupportLevel = 110;

		m_Adapters.AddToTail( entry );
	}

	if ( m_Adapters.Count() == 0 )
	{
		Warning( "shaderapidx11: no DXGI adapters found\n" );
		return INIT_FAILED;
	}

	Msg( "shaderapidx11: %d display adapter(s) found\n", m_Adapters.Count() );
	return INIT_OK;
}

void CShaderDeviceMgrDx11::Shutdown()
{
	s_ShaderDeviceDx11.DestroyDevice();

	for ( int i = 0; i < m_Adapters.Count(); ++i )
	{
		if ( m_Adapters[i].m_pAdapter )
		{
			m_Adapters[i].m_pAdapter->Release();
			m_Adapters[i].m_pAdapter = NULL;
		}
	}
	m_Adapters.Purge();
	m_ModeChangeCallbacks.Purge();

	if ( m_pFactory )
	{
		m_pFactory->Release();
		m_pFactory = NULL;
	}
}

int CShaderDeviceMgrDx11::GetAdapterCount() const
{
	return m_Adapters.Count();
}

void CShaderDeviceMgrDx11::GetAdapterInfo( int adapter, MaterialAdapterInfo_t &info ) const
{
	if ( adapter >= 0 && adapter < m_Adapters.Count() )
	{
		info = m_Adapters[adapter].m_Info;
		return;
	}
	memset( &info, 0, sizeof( info ) );
	info.m_nDXSupportLevel = 110;
	info.m_nMaxDXSupportLevel = 110;
}

bool CShaderDeviceMgrDx11::GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pKeyValues )
{
	// Same contract as the empty backend: the caller pre-seeds the KeyValues
	// with its defaults, and we have no D3DCAPS-style recommendation to
	// override them with (the real negotiation moves to DXGI
	// CheckFeatureLevelSupport in Stage 5).
	return true;
}

void CShaderDeviceMgrDx11::EnumerateModes( int nAdapter, CUtlVector<DXGI_MODE_DESC> &modes ) const
{
	modes.Purge();

	IDXGIAdapter1 *pAdapter = GetAdapter( nAdapter );
	if ( !pAdapter )
		return;

	for ( UINT outputIndex = 0; ; ++outputIndex )
	{
		IDXGIOutput *pOutput = NULL;
		if ( FAILED( pAdapter->EnumOutputs( outputIndex, &pOutput ) ) || !pOutput )
			break;

		UINT nModes = 0;
		if ( SUCCEEDED( pOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &nModes, NULL ) ) && nModes > 0 )
		{
			CUtlVector<DXGI_MODE_DESC> outputModes;
			for ( UINT i = 0; i < nModes; ++i )
				outputModes.AddToTail();
			if ( SUCCEEDED( pOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &nModes, &outputModes[0] ) ) )
			{
				for ( UINT i = 0; i < nModes; ++i )
					modes.AddToTail( outputModes[i] );
			}
		}
		pOutput->Release();
	}
}

int CShaderDeviceMgrDx11::GetModeCount( int nAdapter ) const
{
	CUtlVector<DXGI_MODE_DESC> modes;
	EnumerateModes( nAdapter, modes );
	if ( modes.Count() > 0 )
		return modes.Count();

	// No listed modes (headless/odd drivers): offer the desktop resolution,
	// like the DX9 backend's fallback for outputs with no mode list.
	return 1;
}

void CShaderDeviceMgrDx11::GetModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter, int nMode ) const
{
	if ( !pInfo )
		return;

	pInfo->m_nVersion = SHADER_DISPLAY_MODE_VERSION;
	pInfo->m_nWidth = 0;
	pInfo->m_nHeight = 0;
	pInfo->m_Format = IMAGE_FORMAT_RGBA8888;
	pInfo->m_nRefreshRateNumerator = 0;
	pInfo->m_nRefreshRateDenominator = 1;

	CUtlVector<DXGI_MODE_DESC> modes;
	EnumerateModes( nAdapter, modes );
	if ( nMode >= 0 && nMode < modes.Count() )
	{
		const DXGI_MODE_DESC &mode = modes[nMode];
		pInfo->m_nWidth = (int)mode.Width;
		pInfo->m_nHeight = (int)mode.Height;
		pInfo->m_nRefreshRateNumerator = (int)mode.RefreshRate.Numerator;
		pInfo->m_nRefreshRateDenominator = ( mode.RefreshRate.Denominator != 0 ) ? (int)mode.RefreshRate.Denominator : 1;
		return;
	}

	// Fallback: current desktop bounds (no listed modes: odd/headless drivers).
	IDXGIAdapter1 *pAdapter = GetAdapter( nAdapter );
	IDXGIOutput *pOutput = NULL;
	if ( pAdapter )
		pAdapter->EnumOutputs( 0, &pOutput );
	if ( pOutput )
	{
		DXGI_OUTPUT_DESC outputDesc;
		if ( SUCCEEDED( pOutput->GetDesc( &outputDesc ) ) )
		{
			pInfo->m_nWidth = (int)( outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left );
			pInfo->m_nHeight = (int)( outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top );
		}
		pOutput->Release();
	}
	pInfo->m_nRefreshRateNumerator = 60;
}

void CShaderDeviceMgrDx11::GetCurrentModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter ) const
{
	// The first listed mode is the output's current mode on a DXGI
	// enumeration; GetModeInfo's desktop-bounds fallback covers no-list outputs.
	GetModeInfo( pInfo, nAdapter, 0 );
}

bool CShaderDeviceMgrDx11::SetAdapter( int nAdapter, int nFlags )
{
	// materialsystem calls this before Init(), so the index is only stored
	// here; it is validated when SetMode actually creates the device.
	m_nAdapter = nAdapter;
	return true;
}

CreateInterfaceFn CShaderDeviceMgrDx11::SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode )
{
	if ( !s_ShaderDeviceDx11.CreateDevice( hWnd, nAdapter, mode ) )
		return NULL;
	return ShaderDeviceDx11InterfaceFactory;
}

void CShaderDeviceMgrDx11::AddModeChangeCallback( ShaderModeChangeCallbackFunc_t func )
{
	if ( !func )
		return;
	for ( int i = 0; i < m_ModeChangeCallbacks.Count(); ++i )
	{
		if ( m_ModeChangeCallbacks[i] == func )
			return;
	}
	m_ModeChangeCallbacks.AddToTail( func );
}

void CShaderDeviceMgrDx11::RemoveModeChangeCallback( ShaderModeChangeCallbackFunc_t func )
{
	for ( int i = 0; i < m_ModeChangeCallbacks.Count(); ++i )
	{
		if ( m_ModeChangeCallbacks[i] == func )
		{
			m_ModeChangeCallbacks.Remove( i );
			return;
		}
	}
}

IDXGIAdapter1 *CShaderDeviceMgrDx11::GetAdapter( int nAdapter ) const
{
	if ( nAdapter >= 0 && nAdapter < m_Adapters.Count() )
		return m_Adapters[nAdapter].m_pAdapter;
	return NULL;
}

void CShaderDeviceMgrDx11::NotifyModeChange()
{
	for ( int i = 0; i < m_ModeChangeCallbacks.Count(); ++i )
		m_ModeChangeCallbacks[i]();
}


//-----------------------------------------------------------------------------
//
// CShaderDeviceDx11
//
//-----------------------------------------------------------------------------
CShaderDeviceDx11::CShaderDeviceDx11() : m_Mesh( false ), m_DynamicMesh( true )
{
	m_hWnd = NULL;
	m_pDevice = NULL;
	m_pContext = NULL;
	m_pSwapChain = NULL;
	m_pRTV = NULL;
	m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
	m_nAdapter = 0;
	m_nBackBufferCount = 1;
	m_nWidth = 0;
	m_nHeight = 0;
	m_bDeviceReady = false;
	m_bPresentedSinceClear = false;
	m_bWaitForVSync = false;
	m_szDisplayDeviceName[0] = '\0';
}

bool CShaderDeviceDx11::CreateDevice( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode )
{
	// vid_restart / video-mode changes re-enter SetMode with a live device.
	if ( m_bDeviceReady )
		DestroyDevice();

	if ( !hWnd )
	{
		Warning( "shaderapidx11: SetMode called without a window\n" );
		return false;
	}
	m_hWnd = reinterpret_cast<HWND>( hWnd );
	m_nAdapter = nAdapter;
	m_bWaitForVSync = mode.m_bWaitForVSync;
	// ShaderDeviceInfo_t: 1 or 2 backbuffers (2 = triple buffering).
	m_nBackBufferCount = ( mode.m_nBackBufferCount >= 1 && mode.m_nBackBufferCount <= 2 ) ? mode.m_nBackBufferCount : 1;

	int nWidth = mode.m_DisplayMode.m_nWidth;
	int nHeight = mode.m_DisplayMode.m_nHeight;
	RECT rcClient;
	GetClientRect( m_hWnd, &rcClient );
	if ( nWidth <= 0 || nHeight <= 0 )
	{
		nWidth = (int)( rcClient.right - rcClient.left );
		nHeight = (int)( rcClient.bottom - rcClient.top );
	}
	if ( nWidth <= 0 || nHeight <= 0 )
	{
		Warning( "shaderapidx11: SetMode with a zero-sized window\n" );
		m_hWnd = NULL;
		return false;
	}

	IDXGIAdapter1 *pAdapter = s_ShaderDeviceMgrDx11.GetAdapter( nAdapter );
	if ( !pAdapter )
	{
		Warning( "shaderapidx11: invalid adapter %d\n", nAdapter );
		m_hWnd = NULL;
		return false;
	}

	static const D3D_FEATURE_LEVEL s_FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	HRESULT hr = D3D11CreateDevice(
		pAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		NULL,
		0,
		s_FeatureLevels,
		(UINT)ARRAYSIZE( s_FeatureLevels ),
		D3D11_SDK_VERSION,
		&m_pDevice,
		&featureLevel,
		&m_pContext );
	if ( FAILED( hr ) || !m_pDevice || !m_pContext )
	{
		Warning( "shaderapidx11: D3D11CreateDevice failed (0x%08x)\n", (unsigned int)hr );
		m_hWnd = NULL;
		return false;
	}
	m_FeatureLevel = featureLevel;

	DXGI_SWAP_CHAIN_DESC sd;
	memset( &sd, 0, sizeof( sd ) );
	sd.BufferDesc.Width = (UINT)nWidth;
	sd.BufferDesc.Height = (UINT)nHeight;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 0;	// windowed: unused; fullscreen: DXGI keeps the current refresh
	sd.BufferDesc.RefreshRate.Denominator = 0;
	sd.SampleDesc.Count = 1;					// Stage 3: no MSAA; m_nAASamples lands in Stage 5
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = (UINT)m_nBackBufferCount;
	sd.OutputWindow = m_hWnd;
	sd.Windowed = mode.m_bWindowed ? TRUE : FALSE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = mode.m_bWindowed ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	IDXGIFactory1 *pFactory = s_ShaderDeviceMgrDx11.GetFactory();
	if ( !pFactory )
	{
		Warning( "shaderapidx11: no DXGI factory (mgr Init not run?)\n" );
		DestroyDevice();
		return false;
	}
	hr = pFactory->CreateSwapChain( m_pDevice, &sd, &m_pSwapChain );
	if ( FAILED( hr ) || !m_pSwapChain )
	{
		Warning( "shaderapidx11: CreateSwapChain failed (0x%08x)\n", (unsigned int)hr );
		DestroyDevice();
		return false;
	}

	if ( !CreateBackBufferView() )
	{
		DestroyDevice();
		return false;
	}

	MaterialAdapterInfo_t adapterInfo;
	s_ShaderDeviceMgrDx11.GetAdapterInfo( nAdapter, adapterInfo );
	Q_strncpy( m_szDisplayDeviceName, adapterInfo.m_pDriverName, sizeof( m_szDisplayDeviceName ) );

	m_bDeviceReady = true;
	m_bPresentedSinceClear = false;

	Msg( "shaderapidx11: device ready - feature level %u, %dx%d, %s, %s\n",
		(unsigned int)m_FeatureLevel, m_nWidth, m_nHeight,
		m_szDisplayDeviceName, mode.m_bWindowed ? "windowed" : "fullscreen" );
	return true;
}

void CShaderDeviceDx11::DestroyDevice()
{
	if ( m_pRTV )
	{
		m_pRTV->Release();
		m_pRTV = NULL;
	}
	if ( m_pSwapChain )
	{
		m_pSwapChain->Release();
		m_pSwapChain = NULL;
	}
	if ( m_pContext )
	{
		m_pContext->Release();
		m_pContext = NULL;
	}
	if ( m_pDevice )
	{
		m_pDevice->Release();
		m_pDevice = NULL;
	}
	m_hWnd = NULL;
	m_bDeviceReady = false;
	m_bPresentedSinceClear = false;
	m_nWidth = 0;
	m_nHeight = 0;
}

bool CShaderDeviceDx11::CreateBackBufferView()
{
	if ( !m_pDevice || !m_pSwapChain )
		return false;

	ID3D11Texture2D *pBuffer = NULL;
	HRESULT hr = m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast<void **>( &pBuffer ) );
	if ( FAILED( hr ) || !pBuffer )
	{
		Warning( "shaderapidx11: swapchain GetBuffer failed (0x%08x)\n", (unsigned int)hr );
		return false;
	}
	hr = m_pDevice->CreateRenderTargetView( pBuffer, NULL, &m_pRTV );
	pBuffer->Release();
	if ( FAILED( hr ) || !m_pRTV )
	{
		Warning( "shaderapidx11: CreateRenderTargetView failed (0x%08x)\n", (unsigned int)hr );
		return false;
	}

	DXGI_SWAP_CHAIN_DESC sd;
	memset( &sd, 0, sizeof( sd ) );
	if ( SUCCEEDED( m_pSwapChain->GetDesc( &sd ) ) )
	{
		m_nWidth = (int)sd.BufferDesc.Width;
		m_nHeight = (int)sd.BufferDesc.Height;
	}
	return true;
}

bool CShaderDeviceDx11::CheckAndResize()
{
	if ( !m_hWnd || !m_pSwapChain )
		return false;
	if ( IsIconic( m_hWnd ) )	// minimized (alt-tabbed away): keep buffers, skip work
		return false;

	RECT rcClient;
	GetClientRect( m_hWnd, &rcClient );
	int nWidth = (int)( rcClient.right - rcClient.left );
	int nHeight = (int)( rcClient.bottom - rcClient.top );
	if ( nWidth <= 0 || nHeight <= 0 )
		return false;
	if ( nWidth == m_nWidth && nHeight == m_nHeight )
		return true;

	// Drop our RTV reference before ResizeBuffers (DXGI forbids outstanding
	// references) and rebuild it on the new backbuffer. This covers window
	// resize and alt-tab restore for Stage 3.
	if ( m_pRTV )
	{
		m_pRTV->Release();
		m_pRTV = NULL;
	}
	HRESULT hr = m_pSwapChain->ResizeBuffers( 0, (UINT)nWidth, (UINT)nHeight, DXGI_FORMAT_UNKNOWN, 0 );
	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: ResizeBuffers(%dx%d) failed (0x%08x)\n", nWidth, nHeight, (unsigned int)hr );
		return false;
	}
	if ( !CreateBackBufferView() )
		return false;

	m_bPresentedSinceClear = false;
	s_ShaderDeviceMgrDx11.NotifyModeChange();
	return true;
}

void CShaderDeviceDx11::ClearToSpikeColor()
{
	if ( m_pContext && m_pRTV )
		m_pContext->ClearRenderTargetView( m_pRTV, s_SpikeClearColor );
	m_bPresentedSinceClear = true;
}

void CShaderDeviceDx11::Present()
{
	if ( !m_bDeviceReady )
		return;
	if ( !CheckAndResize() )
		return;	// minimized or a failed resize: don't present

	if ( !m_bPresentedSinceClear )
	{
		// Nobody asked for a clear this frame; present the spike colour
		// anyway so stale contents can never be shown.
		ClearToSpikeColor();
	}
	m_pSwapChain->Present( m_bWaitForVSync ? 1 : 0, 0 );
	m_bPresentedSinceClear = false;
}


//-----------------------------------------------------------------------------
// IShaderDevice: device-lifecycle methods are real (above), everything else
// is the Stage-3 stub surface. Stage references follow phase3_rhi_map.md.
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ReleaseResources()
{
	// D3D11 windowed mode has no default-pool resources to release; the
	// device-removed recovery path is specified when the full API lands.
}

void CShaderDeviceDx11::ReacquireResources()
{
}

ImageFormat CShaderDeviceDx11::GetBackBufferFormat() const
{
	return IMAGE_FORMAT_RGBA8888;	// DXGI_FORMAT_R8G8B8A8_UNORM
}

void CShaderDeviceDx11::GetBackBufferDimensions( int &width, int &height ) const
{
	width = m_nWidth;
	height = m_nHeight;
}

int CShaderDeviceDx11::GetCurrentAdapter() const
{
	return m_nAdapter;
}

bool CShaderDeviceDx11::IsUsingGraphics() const
{
	return m_bDeviceReady;
}

void CShaderDeviceDx11::SpewDriverInfo() const
{
	Warning( "D3D11: %s (feature level %u)\n", m_szDisplayDeviceName, (unsigned int)m_FeatureLevel );
}

int CShaderDeviceDx11::StencilBufferBits() const
{
	return 0;	// no depth/stencil view is created in Stage 3 (Stage 5)
}

bool CShaderDeviceDx11::IsAAEnabled() const
{
	return false;	// Stage 3 always creates a non-MSAA swapchain (Stage 5)
}

void CShaderDeviceDx11::GetWindowSize( int &nWidth, int &nHeight ) const
{
	if ( m_hWnd )
	{
		RECT rcClient;
		GetClientRect( m_hWnd, &rcClient );
		nWidth = (int)( rcClient.right - rcClient.left );
		nHeight = (int)( rcClient.bottom - rcClient.top );
		if ( nWidth > 0 && nHeight > 0 )
			return;
	}
	nWidth = m_nWidth;
	nHeight = m_nHeight;
}

void CShaderDeviceDx11::SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled )
{
	// Stage 5: DXGI fullscreen gamma / shader-side gamma (phase3_rhi_map.md).
}

bool CShaderDeviceDx11::AddView( void *hWnd )
{
	return true;	// multi-window views (Hammer) accepted as no-op until Stage 5
}

void CShaderDeviceDx11::RemoveView( void *hWnd )
{
}

void CShaderDeviceDx11::SetView( void *hWnd )
{
}

IShaderBuffer *CShaderDeviceDx11::CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion )
{
	return NULL;	// Stage 4: D3DCompile with vs_4_0/ps_4_0 profiles
}

VertexShaderHandle_t CShaderDeviceDx11::CreateVertexShader( IShaderBuffer *pShaderBuffer )
{
	return VERTEX_SHADER_HANDLE_INVALID;	// Stage 4
}

void CShaderDeviceDx11::DestroyVertexShader( VertexShaderHandle_t hShader )
{
}

GeometryShaderHandle_t CShaderDeviceDx11::CreateGeometryShader( IShaderBuffer *pShaderBuffer )
{
	return GEOMETRY_SHADER_HANDLE_INVALID;	// Stage 4
}

void CShaderDeviceDx11::DestroyGeometryShader( GeometryShaderHandle_t hShader )
{
}

PixelShaderHandle_t CShaderDeviceDx11::CreatePixelShader( IShaderBuffer *pShaderBuffer )
{
	return PIXEL_SHADER_HANDLE_INVALID;	// Stage 4
}

void CShaderDeviceDx11::DestroyPixelShader( PixelShaderHandle_t hShader )
{
}

IMesh *CShaderDeviceDx11::CreateStaticMesh( VertexFormat_t vertexFormat, const char *pTextureBudgetGroup, IMaterial *pMaterial )
{
	// Stage 5 replaces this with real buffers. Until then the placeholder
	// keeps CMeshBuilder off a NULL deref: studiorender calls this for
	// every static prop during SpawnServer at menu boot
	// (R_StudioBuildMeshGroup). Mirrors CShaderDeviceEmpty.
	return &m_Mesh;
}

void CShaderDeviceDx11::DestroyStaticMesh( IMesh *mesh )
{
}

IVertexBuffer *CShaderDeviceDx11::CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup )
{
	return ( type == SHADER_BUFFER_TYPE_STATIC || type == SHADER_BUFFER_TYPE_STATIC_TEMP ) ? &m_Mesh : &m_DynamicMesh;
}

void CShaderDeviceDx11::DestroyVertexBuffer( IVertexBuffer *pVertexBuffer )
{
}

IIndexBuffer *CShaderDeviceDx11::CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup )
{
	switch( bufferType )
	{
	case SHADER_BUFFER_TYPE_STATIC:
	case SHADER_BUFFER_TYPE_STATIC_TEMP:
		return &m_Mesh;
	default:
	case SHADER_BUFFER_TYPE_DYNAMIC:
	case SHADER_BUFFER_TYPE_DYNAMIC_TEMP:
		return &m_DynamicMesh;
	}
}

void CShaderDeviceDx11::DestroyIndexBuffer( IIndexBuffer *pIndexBuffer )
{
}

IVertexBuffer *CShaderDeviceDx11::GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered )
{
	return &m_DynamicMesh;
}

IIndexBuffer *CShaderDeviceDx11::GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered )
{
	return &m_Mesh;
}

void CShaderDeviceDx11::EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo )
{
}

void CShaderDeviceDx11::RefreshFrontBufferNonInteractive()
{
}

void CShaderDeviceDx11::HandleThreadEvent( uint32 threadEvent )
{
	// The DX9 events (release/evict/reset/acquire) exist for device-loss
	// handling D3D11 windowed mode doesn't do; mode re-entry goes through
	// CreateDevice's re-create path instead.
}

// DoStartupShaderPreloading is a GL-abstraction-only hook (declared in
// IShaderDevice behind the fake-D3D9-over-GL define); this backend is
// Windows/DX11 and never compiles it.

char *CShaderDeviceDx11::GetDisplayDeviceName()
{
	return m_szDisplayDeviceName;
}


//-----------------------------------------------------------------------------
// Backend hooks: the stub seam's SetMode/ClearBuffers land here
//-----------------------------------------------------------------------------
bool CShaderAPIBackendHooksDx11::SetMode( void *hwnd, int nAdapter, const ShaderDeviceInfo_t &info )
{
	return s_ShaderDeviceDx11.CreateDevice( hwnd, nAdapter, info );
}

void CShaderAPIBackendHooksDx11::ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight )
{
	// Stage 3: one known clear colour proves the swapchain path end to end.
	// Depth/stencil have no attachments yet (Stage 5), and the engine's
	// requested colour is honoured from Stage 5 on.
	if ( bClearColor )
		s_ShaderDeviceDx11.ClearToSpikeColor();
}

bool CShaderAPIBackendHooksDx11::CanDownloadTextures() const
{
	// Mirrors CShaderAPIDx8::CanDownloadTextures (IsActive): downloads are
	// possible once our device exists. The standalone empty build keeps its
	// unconditional false.
	return s_ShaderDeviceDx11.IsUsingGraphics();
}
