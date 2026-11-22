// SPDX-License-Identifier: MIT
#include "GeometryCommandHelpers.h"
#include "Library/Math/MathUtils.h"

bool NGeometryCommand::AreFinite( const float32* Values, int32 Count )
{
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		if ( !NMath::IsFinite( Values[Index] ) )
		{
			return false;
		}
	}
	return true;
}

FMatrix4x4 NGeometryCommand::ToMatrix( const FDrawMatrixData& Data )
{
	FMatrix4x4 Result; // 最終列は(0,0,0,1)。
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 3; ++Column )
		{
			Result.m[Row][Column] = Data.M[Row * 3 + Column];
		}
	}
	return Result;
}

FDrawVertex3D NGeometryCommand::Vertex( const float32 ( &Position )[3], FDrawRgb Color )
{
	FDrawVertex3D Result; // 法線と画像座標は使わない。
	FMemory::Memcpy( Result.Position, Position, sizeof( Position ) );
	Result.Diffuse[0] = Color.R;
	Result.Diffuse[1] = Color.G;
	Result.Diffuse[2] = Color.B;
	return Result;
}

void NGeometryCommand::SetSpecular( TArray<FDrawVertex3D>& Vertices, FDrawRgb Specular )
{
	for ( FDrawVertex3D& Value : Vertices )
	{
		Value.Specular[0] = Specular.R;
		Value.Specular[1] = Specular.G;
		Value.Specular[2] = Specular.B;
	}
}

FColor NGeometryCommand::ToColor( const float32 ( &Values )[3] )
{
	return FColor( Values[0], Values[1], Values[2], 1.0f );
}

float32 NGeometryCommand::GetFogVisibility( const FDrawFogData& Fog, float32 Depth )
{
	float32 Visibility = 1.0f; // 通常の（フォグなしの）見え方。
	switch ( static_cast<EDrawFogMode>( Fog.Mode ) )
	{
	case EDrawFogMode::Exponential:
		Visibility = NMath::Exp( -Fog.Density * Depth );
		break;
	case EDrawFogMode::ExponentialSquared:
		Visibility = NMath::Exp( -( Fog.Density * Depth ) * ( Fog.Density * Depth ) );
		break;
	case EDrawFogMode::Linear:
		Visibility = Fog.End > Fog.Start ? ( Fog.End - Depth ) / ( Fog.End - Fog.Start ) : ( Depth < Fog.End ? 1.0f : 0.0f );
		break;
	default:
		break;
	}
	return NMath::IsFinite( Visibility ) ? NMath::Clamp( Visibility, 0.0f, 1.0f ) : 1.0f;
}

float32 NGeometryCommand::GetHeightFogVisibility( const FDrawFogData& Fog, float32 Height )
{
	float32 Visibility = 1.0f; // 通常の（フォグなしの）見え方。
	const float32 Below = NMath::Max( 0.0f, Fog.Start - Height ); // Start より下の深さ。
	switch ( static_cast<EDrawFogMode>( Fog.Mode ) )
	{
	case EDrawFogMode::Exponential:
		Visibility = NMath::Exp( -Fog.Density * Below );
		break;
	case EDrawFogMode::ExponentialSquared:
		Visibility = NMath::Exp( -( Fog.Density * Below ) * ( Fog.Density * Below ) );
		break;
	case EDrawFogMode::Linear:
		Visibility = 1.0f - NMath::Clamp( Fog.End != Fog.Start ? ( Height - Fog.Start ) / ( Fog.End - Fog.Start ) : ( Height < Fog.End ? 1.0f : 0.0f ), 0.0f, 1.0f );
		break;
	default:
		break;
	}
	return NMath::IsFinite( Visibility ) ? NMath::Clamp( Visibility, 0.0f, 1.0f ) : 1.0f;
}

int32 NGeometryCommand::ToByte( float32 Value )
{
	return NMath::IsFinite( Value ) ? static_cast<int32>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f + 0.5f ) : 0;
}
