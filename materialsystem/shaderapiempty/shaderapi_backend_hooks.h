//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Backend hook seam between the shared empty-shader stub
//          implementation and a real backend project that compiles
//          shaderapiempty.cpp into itself (see
//          materialsystem/shaderapidx11).
//
//          shaderapiempty.cpp implements the full IShaderDevice /
//          IShaderDeviceMgr / IShaderAPI / IShaderShadow seam with
//          harmless stubs. A backend that needs the few calls which must
//          reach real hardware - device/swapchain creation and buffer
//          clears - compiles that file with SHADERAPIEMPTY_BACKEND_BUILD
//          (which stops the empty device/device-mgr from registering
//          themselves) and installs its hooks here. When
//          shaderapiempty.cpp is built as the standalone
//          shaderapiempty.dll, no hooks are installed and behaviour is
//          unchanged.
//
//===========================================================================//

#ifndef SHADERAPI_BACKEND_HOOKS_H
#define SHADERAPI_BACKEND_HOOKS_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderapi/IShaderDevice.h"

abstract_class IShaderAPIBackendHooks
{
public:
	// Creates the device + swapchain for the mode materialsystem selected.
	// Replaces CShaderAPIEmpty::SetMode.
	virtual bool SetMode( void *hwnd, int nAdapter, const ShaderDeviceInfo_t &info ) = 0;

	// Responds to materialsystem's per-frame clear requests.
	// Replaces CShaderAPIEmpty::ClearBuffers.
	virtual void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight ) = 0;

	// Whether texture downloads are possible right now (device active).
	// Replaces CShaderAPIEmpty::CanDownloadTextures, which is false in the
	// standalone empty build by design.
	virtual bool CanDownloadTextures() const = 0;
};

// Defined in shaderapiempty.cpp; NULL unless a backend project installs its hooks.
extern IShaderAPIBackendHooks *g_pShaderAPIBackendHooks;

#endif // SHADERAPI_BACKEND_HOOKS_H
