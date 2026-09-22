//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================

#include "tier0/tslist.h"
#include <list>
#include <stdlib.h>
#if defined( _X360 )
#endif

#include "unitlib/unitlib.h"

DEFINE_TESTSUITE( TSListTestSuite )

DEFINE_TESTCASE( TSListTest, TSListTestSuite )
{
	RunTSListTests( 50000 );

	RunTSQueueTests( 50000 );
}
