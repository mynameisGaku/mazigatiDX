// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsContactTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// NContactRules（Physics の内部用）
//   接触の通知の「誰に届けるか」「どの組か」「どれだけの勢いか」を決める規則。物理エンジンの型を使わない。
//=============================================================================
namespace NContactRules
{
	// 2 つの剛体の組を 1 つの値にする（順は問わない）。キーは BodyID::GetIndexAndSequenceNumber
	inline uint64 MakePairKey( uint32 KeyA, uint32 KeyB )
	{
		return KeyA < KeyB ? ( static_cast<uint64>( KeyA ) << 32 ) | KeyB : ( static_cast<uint64>( KeyB ) << 32 ) | KeyA;
	}

	// 受け取り側の絞り込みを通るか。
	//   bHasBody のとき、BodyKey の剛体が組のどちらかでなければ届けない。
	//   LayerMask は、どちらかの剛体のレイヤーが含まれていれば届ける
	inline bool Accepts( bool bHasBody, uint32 BodyKey, uint32 LayerMask, uint32 KeyA, uint32 LayerA, uint32 KeyB, uint32 LayerB )
	{
		if ( bHasBody && BodyKey != KeyA && BodyKey != KeyB ) return false;
		return ( LayerMask & ( NCollisionLayer::ToMask( LayerA ) | NCollisionLayer::ToMask( LayerB ) ) ) != 0;
	}

	// 法線方向の近づく速さ。Normal は A から B へ向かう向き。A が B へ近づいているとき正
	inline float32 ClosingSpeed( const FVector3& VelocityA, const FVector3& VelocityB, const FVector3& Normal )
	{
		return FVector3::Dot( VelocityA - VelocityB, Normal );
	}
}
