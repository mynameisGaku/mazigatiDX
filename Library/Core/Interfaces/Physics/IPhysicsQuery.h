// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Physics/PhysicsQueryTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Quaternion.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// IPhysicsQuery
//   物理ワールドへの空間クエリ（レイ・重なり・形状の掃引）。読むだけで、剛体は動かさない。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースだけを知ればよく、
//   物理エンジンの型を include しない。
//
//   IPhysicsQuery* Query = GetScene()->GetSubsystem<IPhysicsQuery>();   // 物理を使うシーンにだけある（無ければ nullptr）
//   FRayHit Hit;
//   if ( Query->RayCast( Eye, Forward, 100.0f, Hit ) ) Use( Hit.UserData, Hit.Position );
//
//   - Direction は長さを問わない（中で長さ 1 にする）。零ベクトル・NaN・無限大、MaxDistance が 0 以下・NaN・無限大のときは
//     何にも当たらなかったことにする（結果は空）。形状の大きさが 0 以下・NaN・無限大のときも同じ
//   - メッシュの裏面: RayCast / RayCastAll / Sweep は無視する（表から入ったときだけ当たる）。Overlap は裏側にいても重なりに数える
//   - 結果の剛体は CPhysicsSystem::CreateBody で作ったものだけ（Jolt を直接触って作った剛体は出ない）
//   - 物理の Update と同時には呼ばない（呼ぶのは、同じスレッドの Update の合間）
//=============================================================================
class IPhysicsQuery
{
public:
	virtual ~IPhysicsQuery() = default;

	// 最初に当たったもの。当たれば true
	virtual bool RayCast( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, FRayHit& OutHit,
						  const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const = 0;

	// 当たったものすべて（近い順）。OutHits は先に空にする。返り値は件数。メッシュは三角形ごとに 1 件になる
	virtual int32 RayCastAll( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, TArray<FRayHit>& OutHits,
							  const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const = 0;

	// 形を置いたとき、重なる剛体（同じ剛体は 1 回だけ）。OutHits は先に空にする。返り値は件数
	virtual int32 Overlap( const FQueryShape& Shape, const FVector3& Position, const FQuaternion& Rotation, TArray<FOverlapHit>& OutHits,
						   const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const = 0;

	// 形を Start から Direction へ MaxDistance まで動かしたとき、最初にぶつかるもの。ぶつかれば true
	virtual bool Sweep( const FQueryShape& Shape, const FVector3& Start, const FQuaternion& Rotation, const FVector3& Direction, float32 MaxDistance,
						FShapeCastHit& OutHit, const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const = 0;
};
