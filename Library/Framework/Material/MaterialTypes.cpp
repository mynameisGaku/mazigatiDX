// SPDX-License-Identifier: MIT
#include "MaterialTypes.h"

FMaterialCapabilities NMaterial::DefaultCapabilities()
{
	return FMaterialCapabilities();
}

int32 NMaterial::NumComponents( EMaterialValueType Type )
{
	switch ( Type )
	{
	case EMaterialValueType::Float:
		return 1;
	case EMaterialValueType::Float2:
		return 2;
	case EMaterialValueType::Float3:
		return 3;
	case EMaterialValueType::Float4:
		return 4;
	default:
		return 0;
	}
}

EMaterialValueType NMaterial::TypeFromComponents( int32 NumComponents )
{
	switch ( NumComponents )
	{
	case 2:
		return EMaterialValueType::Float2;
	case 3:
		return EMaterialValueType::Float3;
	case 4:
		return EMaterialValueType::Float4;
	default:
		return EMaterialValueType::Float;
	}
}

const char* NMaterial::ToString( EMaterialDomain Domain )
{
	switch ( Domain )
	{
	case EMaterialDomain::UI:
		return "UI";
	case EMaterialDomain::Surface:
		return "Surface";
	case EMaterialDomain::Particle:
		return "Particle";
	default:
		return "PostProcess";
	}
}

const char* NMaterial::ToString( EMaterialShading Shading )
{
	switch ( Shading )
	{
	case EMaterialShading::Unlit:
		return "Unlit";
	case EMaterialShading::DefaultLit:
		return "DefaultLit";
	default:
		return "Substrate";
	}
}

const char* NMaterial::ToString( EMaterialValueType Type )
{
	switch ( Type )
	{
	case EMaterialValueType::Float:
		return "Float";
	case EMaterialValueType::Float2:
		return "Float2";
	case EMaterialValueType::Float3:
		return "Float3";
	case EMaterialValueType::Float4:
		return "Float4";
	case EMaterialValueType::Texture:
		return "Texture";
	default:
		return "Slab";
	}
}

const char* NMaterial::ToString( EMaterialSeverity Severity )
{
	switch ( Severity )
	{
	case EMaterialSeverity::Info:
		return "Info";
	case EMaterialSeverity::Warning:
		return "Warning";
	default:
		return "Error";
	}
}

bool NMaterial::FromString( const FString& Text, EMaterialDomain& OutDomain )
{
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		const EMaterialDomain Candidate = static_cast<EMaterialDomain>( Index );
		if ( Text == ToString( Candidate ) )
		{
			OutDomain = Candidate;
			return true;
		}
	}
	return false;
}

bool NMaterial::FromString( const FString& Text, EMaterialShading& OutShading )
{
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		const EMaterialShading Candidate = static_cast<EMaterialShading>( Index );
		if ( Text == ToString( Candidate ) )
		{
			OutShading = Candidate;
			return true;
		}
	}
	return false;
}

bool NMaterial::FromString( const FString& Text, EMaterialValueType& OutType )
{
	for ( int32 Index = 0; Index < 6; ++Index )
	{
		const EMaterialValueType Candidate = static_cast<EMaterialValueType>( Index );
		if ( Text == ToString( Candidate ) )
		{
			OutType = Candidate;
			return true;
		}
	}
	return false;
}

bool NMaterial::HasError( const TArray<FMaterialDiagnostic>& Diagnostics )
{
	for ( int32 Index = 0; Index < Diagnostics.Num(); ++Index )
	{
		if ( Diagnostics[Index].Severity == EMaterialSeverity::Error )
		{
			return true;
		}
	}
	return false;
}
