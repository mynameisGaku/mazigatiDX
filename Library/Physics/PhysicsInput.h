// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Quaternion.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// NPhysicsInput（Physics の内部用）
//   ゲームから渡された値を、物理エンジンへ渡す前に確かめる道具。空間クエリと関節で共有する。
//=============================================================================
namespace NPhysicsInput
{
	inline bool IsFiniteVector( const FVector3& V )
	{
		return NMath::IsFinite( V.x ) && NMath::IsFinite( V.y ) && NMath::IsFinite( V.z );
	}

	inline bool IsFiniteRotation( const FQuaternion& Q )
	{
		return NMath::IsFinite( Q.x ) && NMath::IsFinite( Q.y ) && NMath::IsFinite( Q.z ) && NMath::IsFinite( Q.w );
	}

	// 向きの長さを 1 にする。大きな成分で二乗がオーバーフローしないよう、先に最大の成分で割る。
	// 零ベクトル・NaN・無限大なら false
	inline bool NormalizeDirection( const FVector3& Direction, FVector3& OutDirection )
	{
		if ( !IsFiniteVector( Direction ) ) return false;

		const float32 Largest = NMath::Max( NMath::Abs( Direction.x ), NMath::Max( NMath::Abs( Direction.y ), NMath::Abs( Direction.z ) ) );
		if ( Largest < 1.0e-12f ) return false;

		OutDirection = ( Direction / Largest ).Normalized();
		return true;
	}
}
