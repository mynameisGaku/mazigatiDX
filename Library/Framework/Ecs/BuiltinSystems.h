#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// 組み込みのデータコンポーネントとシステム（全シーンに自動で入る）
//   オブジェクトに付けるだけで、毎フレーム自動で、ジョブシステムのワーカーを使って並列に動く。Tick を書く必要はない。
//
//   void COrbitMarker::OnSpawn()
//   {
//       AddComponent<CTransformComponent>();
//       AddData( FAngularMotion{ FVector3::Forward, NMath::ToRadians( 90.0f ) } );   // Z 軸まわりに毎秒 90 度回る
//   }
//
//   動かす対象は CTransformComponent のローカル値（親からの相対）。CTransformComponent が無いオブジェクトでは何も起きない。
//   PreUpdate フェーズ（OnTick の後、各オブジェクトの Tick の前）で動く。
//
//   アニメーション:
//   FSpriteAnimator（スプライトシート）と FModelAnimator（3D モデル）も、AddData でオブジェクトに付けるだけで、
//   毎フレームの更新（PreUpdate）と描画（描画システム。CTransformComponent のワールド値で描く）が自動で動く。
//   自分で描きたいときは、それぞれの bAutoDraw を false にする。詳しくは SpriteAnimator.h / ModelAnimator.h を参照。
//
//   画像を貼った四角形:
//   FBillboard（看板・草・光の玉・床の模様。カメラの方を向く / Y 軸だけ向く / 向き固定）も、AddData でオブジェクトに付けるだけで描かれる。Billboard.h を参照。
//=============================================================================

// 一定の角速度で回る（ローカル回転に、毎フレーム Axis まわりの回転を掛ける）
struct FAngularMotion
{
	FVector3 Axis = FVector3::Forward;
	float32 RadiansPerSecond = 0.0f;
};

// 一定の速度で動く（ローカル位置に、毎フレーム Velocity * DeltaTime を足す。親の空間での速度）
struct FLinearMotion
{
	FVector3 Velocity = FVector3::Zero;
};

namespace NEcs
{
	class FSystemScheduler;

	// 組み込みのシステムを登録する（CScene の生成時に呼ばれる）
	void RegisterBuiltinSystems( FSystemScheduler& Scheduler );
}
