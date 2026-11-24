// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Asset/Model/Model.h"
#include "Library/Framework/Render/MaterialEffect/ModelMaterialEffectRenderer.h"

// モデルを読み込み、個体ごとの状態を変えて描く。記録中には読み込まない。
class CStaticModelGallery : public CGameObject
{
public:
	// 元データを読み、二つの個体を用意する。
	void OnSpawn() override;
	// 回転用の時計を進める。
	void Tick( float32 DeltaTime ) override;
	// 読み取り済みのモデルを共通命令へ記録する。
	void Draw( FDrawContext& Draw ) override;
private:
	FModelMaterialEffectRenderer MaterialEffect; // 一材質モデルの効果を比較する。
	bool bMaterialEffectRequested = false; // 比較用の描画を選んだか。
	TSharedPtr<CModel> Source; // 元の形状と材質。
	TSharedPtr<CModelInstance> Solid; // 不透明な個体。
	TSharedPtr<CModelInstance> Faded; // 半透明な別個体。
	float32 Time = 0; // 回転の秒数。
};
