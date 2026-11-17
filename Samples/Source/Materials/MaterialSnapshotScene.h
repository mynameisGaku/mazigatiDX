// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Scene/SceneDrawSnapshot.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

class CMaterial;
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
	FSceneDrawSnapshot Snapshot; // パラメーターと資源が固定された描画。
	TArray<FDrawBuffer> Reference; // 並べ替えを使わない正しい手動順。
	TArray<TSharedPtr<CMaterial>> LiveMaterials; // 比較用の変更後の材質だけを保持する。
	bool bManual = false; // referenceとlive-mutatedの手動描画を選ぶ値。
};
