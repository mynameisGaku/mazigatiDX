// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/AI/AIContext.h"
#include "Library/Framework/AI/Registry/ParamSchema.h"

//=============================================================================
// AI の部品の基底
//   ゲーム側が C++ のクラスとして書き、名前で登録する（NAI::RegisterStateAction など）。JSON のアセットは、名前とパラメータで部品を指す。
//
//   - 部品は、アセットを使うオブジェクトごとに 1 つずつ作られる（実行時の状態をメンバーに持ってよい）
//   - パラメータは Initialize で 1 回だけ読んで、メンバーに持つ（毎フレームの名前引きをしない）
//   - 副作用（再生中の効果・移動指示など）は、OnExit で後始末する
//=============================================================================
class FAIPart
{
public:
	virtual ~FAIPart() = default;

	// 作った直後に 1 回。登録したスキーマで読んだパラメータが渡される
	virtual void Initialize( const FParamSet& /*Params*/ ) {}
};

// ステートの処理（入るとき・いる間・出るとき）
class FAIStateAction : public FAIPart
{
public:
	virtual void OnEnter( FAIContext& /*Context*/ ) {}
	virtual void OnTick( FAIContext& /*Context*/, float32 /*DeltaTime*/ ) {}
	virtual void OnExit( FAIContext& /*Context*/ ) {}
	// アニメーションの出来事（OnClipEvent で指した名前）が起きたとき。OnClipEvent に書く処理は、これを実装する（"Hit" で当たり判定を出す、など）
	virtual void OnEvent( FAIContext& /*Context*/, const FString& /*EventName*/ ) {}
};

// 遷移の条件になる判定
class FAICondition : public FAIPart
{
public:
	virtual bool Evaluate( FAIContext& Context ) = 0;
};
