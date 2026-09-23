#include "shaderlib/cshader.h"
class screenspaceeffect_vs20_Static_Index
{
private:
	int m_nMOVIEAPPCHOOSER;
#ifdef _DEBUG
	bool m_bMOVIEAPPCHOOSER;
#endif
public:
	void SetMOVIEAPPCHOOSER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nMOVIEAPPCHOOSER = i;
#ifdef _DEBUG
		m_bMOVIEAPPCHOOSER = true;
#endif
	}
	void SetMOVIEAPPCHOOSER( bool i )
	{
		m_nMOVIEAPPCHOOSER = i ? 1 : 0;
#ifdef _DEBUG
		m_bMOVIEAPPCHOOSER = true;
#endif
	}
public:
	screenspaceeffect_vs20_Static_Index( )
	{
#ifdef _DEBUG
		m_bMOVIEAPPCHOOSER = true;
#endif // _DEBUG
		m_nMOVIEAPPCHOOSER =  0;
	}
	int GetIndex()
	{
		// Asserts to make sure that we aren't using any skipped combinations.
		// Asserts to make sure that we are setting all of the combination vars.
#ifdef _DEBUG
		bool bAllStaticVarsDefined = m_bMOVIEAPPCHOOSER;
		Assert( bAllStaticVarsDefined );
#endif // _DEBUG
		return ( 1 * m_nMOVIEAPPCHOOSER ) + 0;
	}
};
#define shaderStaticTest_screenspaceeffect_vs20 0
class screenspaceeffect_vs20_Dynamic_Index
{
public:
	screenspaceeffect_vs20_Dynamic_Index()
	{
	}
	int GetIndex()
	{
		// Asserts to make sure that we aren't using any skipped combinations.
		// Asserts to make sure that we are setting all of the combination vars.
#ifdef _DEBUG
#endif // _DEBUG
		return 0;
	}
};
#define shaderDynamicTest_screenspaceeffect_vs20 0
