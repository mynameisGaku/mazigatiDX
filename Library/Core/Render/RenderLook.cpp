#include "RenderLook.h"

namespace
{
	// 大文字小文字を問わずに、同じ文字列か
	bool EqualsIgnoreCase( const char* A, const char* B )
	{
		while ( *A && *B )
		{
			const char CharA = *A >= 'A' && *A <= 'Z' ? static_cast<char>( *A - 'A' + 'a' ) : *A;
			const char CharB = *B >= 'A' && *B <= 'Z' ? static_cast<char>( *B - 'A' + 'a' ) : *B;
			if ( CharA != CharB )
			{
				return false;
			}
			++A;
			++B;
		}
		return *A == *B;
	}
}

bool NRenderLook::TryParse( const char* Text, ERenderLook& OutLook )
{
	if ( !Text )
	{
		return false;
	}
	if ( EqualsIgnoreCase( Text, "compat" ) )
	{
		OutLook = ERenderLook::Compat;
		return true;
	}
	if ( EqualsIgnoreCase( Text, "standard" ) )
	{
		OutLook = ERenderLook::Standard;
		return true;
	}
	return false;
}

const char* NRenderLook::ToString( ERenderLook Look )
{
	return Look == ERenderLook::Standard ? "standard" : "compat";
}
