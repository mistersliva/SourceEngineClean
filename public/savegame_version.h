//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#if !defined( SAVEGAME_VERSION_H )
#define SAVEGAME_VERSION_H
#ifdef _WIN32
#pragma once
#endif

// Save files serialize native-width pointer records - the FIELD_POINTER
// datadesc fields and the vphysics pointer-association blobs are written as
// sizeof(void*) bytes (game/shared/saverestore.cpp,
// vphysics/vphysics_saverestore.cpp) - so a save written by a 32-bit build
// cannot be restored by a 64-bit build: the restore path reads a
// native-width pointer per record and a 4-vs-8 byte mismatch desynchronizes
// the stream mid-block instead of failing cleanly. Folding the pointer width
// into the save version makes the existing version check reject cross-arch
// files up front (CSaveRestore::SaveReadHeader, engine/host_saverestore.cpp)
// instead of parsing them into garbage. There is no migration path for
// 32-bit saves (Phase 2 decision 2); see docs/modernization/phase2.md,
// Stage 6.
#define	SAVEGAME_VERSION_BASE		0x0073		// Version 0.73
#define	SAVEGAME_VERSION_ARCH_FLAG	0x8000		// set in saves written by 64-bit builds

// Same test tier0/platform.h uses to define PLATFORM_64BITS, plus waf's
// force-define of it (wscript), spelled out so this header needs no includes:
// it is pulled in before tier0/platform.h in some translation units.
#if defined( PLATFORM_64BITS ) || defined( __x86_64__ ) || defined( _WIN64 ) || defined( __aarch64__ )
#define	SAVEGAME_VERSION		( SAVEGAME_VERSION_BASE | SAVEGAME_VERSION_ARCH_FLAG )
#else
#define	SAVEGAME_VERSION		SAVEGAME_VERSION_BASE
#endif

#endif // SAVEGAME_VERSION_H
