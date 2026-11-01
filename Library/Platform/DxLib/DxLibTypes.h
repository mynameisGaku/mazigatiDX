// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// NDxLibTypes
//   フレームワークの数学・色の型と、DxLib の型（VECTOR / MATRIX / FLOAT2 / FLOAT4 / COLOR_F / COLOR_U8 と、GetColor の色の値）の変換。
//   DxLib の型は、この Platform/DxLib の中だけで使う（フレームワークの型は、DxLib を知らない）。変換は、いつも明示して呼ぶ。
//
//   DrawSphere3D( NDxLibTypes::ToDxLib( Center ), Radius, 16, NDxLibTypes::ToColorCode( Color ), ... );
//   const FMatrix4x4 View = NDxLibTypes::FromDxLib( GetCameraViewMatrix() );
//=============================================================================
namespace NDxLibTypes
{
	FORCEINLINE VECTOR ToDxLib( const FVector3& V )
	{
		return VECTOR { V.x, V.y, V.z };
	}

	FORCEINLINE FVector3 FromDxLib( const VECTOR& V )
	{
		return FVector3( V.x, V.y, V.z );
	}

	// 倍精度の VECTOR_D / MATRIX_D
	FORCEINLINE VECTOR_D ToDxLibDouble( const FVector3& V )
	{
		return VECTOR_D { V.x, V.y, V.z };
	}

	FORCEINLINE FVector3 FromDxLib( const VECTOR_D& V )
	{
		return FVector3( static_cast<float32>( V.x ), static_cast<float32>( V.y ), static_cast<float32>( V.z ) );
	}

	FORCEINLINE FLOAT2 ToDxLib( const FVector2& V )
	{
		return FLOAT2 { V.x, V.y };
	}

	FORCEINLINE FVector2 FromDxLib( const FLOAT2& V )
	{
		return FVector2( V.u, V.v );
	}

	FORCEINLINE FLOAT4 ToDxLib( const FVector4& V )
	{
		return FLOAT4 { V.x, V.y, V.z, V.w };
	}

	FORCEINLINE FVector4 FromDxLib( const FLOAT4& V )
	{
		return FVector4( V.x, V.y, V.z, V.w );
	}

	// 行列は、規約が同じ（行ベクトル・左手系）なので、要素をそのまま写す
	FORCEINLINE MATRIX ToDxLib( const FMatrix4x4& M )
	{
		MATRIX Result {};
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				Result.m[Row][Column] = M.m[Row][Column];
			}
		}
		return Result;
	}

	FORCEINLINE FMatrix4x4 FromDxLib( const MATRIX& M )
	{
		FMatrix4x4 Result;
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				Result.m[Row][Column] = M.m[Row][Column];
			}
		}
		return Result;
	}

	FORCEINLINE MATRIX_D ToDxLibDouble( const FMatrix4x4& M )
	{
		MATRIX_D Result {};
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				Result.m[Row][Column] = M.m[Row][Column];
			}
		}
		return Result;
	}

	FORCEINLINE FMatrix4x4 FromDxLib( const MATRIX_D& M )
	{
		FMatrix4x4 Result;
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				Result.m[Row][Column] = static_cast<float32>( M.m[Row][Column] );
			}
		}
		return Result;
	}

	FORCEINLINE COLOR_F ToDxLib( const FColor& C )
	{
		return COLOR_F { C.r, C.g, C.b, C.a };
	}

	FORCEINLINE FColor FromDxLib( const COLOR_F& C )
	{
		return FColor( C.r, C.g, C.b, C.a );
	}

	// COLOR_U8 のメンバーの並びは b, g, r, a
	FORCEINLINE COLOR_U8 ToDxLib( const FColor32& C )
	{
		return COLOR_U8 { C.b, C.g, C.r, C.a };
	}

	FORCEINLINE FColor32 FromDxLib( const COLOR_U8& C )
	{
		return FColor32( C.r, C.g, C.b, C.a );
	}

	// DrawBox などに渡す色の値（GetColor）。アルファは使われない。DxLib_Init のあとに呼ぶこと
	FORCEINLINE unsigned int ToColorCode( const FColor32& C )
	{
		return GetColor( C.r, C.g, C.b );
	}

	// GetColor の値から戻す（アルファは 255）
	FORCEINLINE FColor32 FromColorCode( unsigned int Code )
	{
		int Red = 0;
		int Green = 0;
		int Blue = 0;
		GetColor2( Code, &Red, &Green, &Blue );
		return FColor32( static_cast<uint8>( Red ), static_cast<uint8>( Green ), static_cast<uint8>( Blue ), 255 );
	}
}
