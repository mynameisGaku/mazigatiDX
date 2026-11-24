// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Scene/SceneDrawSnapshot.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

class CTexture;
class CMaterial;
class CMaterialShadowMap;
class IDrawExecutor;

// 既存の三種類の材質を固定し、ジョブで記録した一覧を手動基準と比較する見本。
class CMaterialSnapshotScene
{
public:
	// 準備と材質の固定はゲームスレッドで行う。失敗時はfalse。
	bool Initialize( const FString& Mode );
	// 確定した一覧か手動の命令列を、通常のシーン描画位置で再生する。
	void Draw( IDrawExecutor& Executor ) const;
private:
	// 派生画像と、同じ画素を持つ独立画像の材質効果を比較する。
	bool InitializeEffectRegion( bool bRegion );
	// しきい値の両側の画素と、奥にある半透明の深度判定を検査する。
	bool InitializeMasked();
	// 影の深度画像で穴と奥の形、影シェーダーの切り替えを確認する。
	bool InitializeMaskedShadow();
	TSharedPtr<CMaterialShadowMap> MaskShadow; // 深度画像と通常の影シェーダーを再生まで所有する。
	FSceneDrawSnapshot Snapshot; // パラメーターと資源が固定された描画。
	TArray<FDrawBuffer> Reference; // 並べ替えを使わない正しい手動順。
	TArray<TSharedPtr<CMaterial>> LiveMaterials; // 比較用の変更後の材質だけを保持する。
	TArray<TSharedPtr<CTexture>> EffectImages; // 画像と派生元を再生まで保持する。
	bool bManual = false; // referenceとlive-mutatedの手動描画を選ぶ値。
};
