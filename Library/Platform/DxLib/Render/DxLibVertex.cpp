// SPDX-License-Identifier: MIT
#include "DxLibVertex.h"

#include "Library/Math/MathUtils.h"

namespace
{
	template<size_t Count>
	bool AreFinite( const float32 ( &Values )[Count] )
	{
		for ( size_t Index = 0; Index < Count; ++Index )
		{
			if ( !NMath::IsFinite( Values[Index] ) )
			{
				return false;
			}
		}
		return true;
	}
}

bool NDxLibVertex::ToDxVertex( const FDrawVertex3D& Source, VERTEX3D& OutVertex )
{
	if ( !AreFinite( Source.Position ) || !AreFinite( Source.Normal ) || !NMath::IsFinite( Source.U ) || !NMath::IsFinite( Source.V ) || !NMath::IsFinite( Source.SubU ) || !NMath::IsFinite( Source.SubV ) )
	{
		return false;
	}
	OutVertex = VERTEX3D {};
	OutVertex.pos = VGet( Source.Position[0], Source.Position[1], Source.Position[2] );
	OutVertex.norm = VGet( Source.Normal[0], Source.Normal[1], Source.Normal[2] );
	OutVertex.dif = GetColorU8( Source.Diffuse[0], Source.Diffuse[1], Source.Diffuse[2], Source.Diffuse[3] );
	OutVertex.spc = GetColorU8( Source.Specular[0], Source.Specular[1], Source.Specular[2], Source.Specular[3] );
	OutVertex.u = Source.U;
	OutVertex.v = Source.V;
	OutVertex.su = Source.SubU;
	OutVertex.sv = Source.SubV;
	return true;
}
