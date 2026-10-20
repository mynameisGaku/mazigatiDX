// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_SOURCE_SCENE_WORLDMESHMATERIALSELFTESTSUPPORT_H
#define FRAMEWORK_SOURCE_SCENE_WORLDMESHMATERIALSELFTESTSUPPORT_H

#include "AssetSelfTestSupport.h"

// 保存・実行側が実装するメッシュ材質テストの引数型。
class CAssetManager;
class IDrawExecutor;

// 編集メッシュの時間変化材質に関する自己確認の宣言。
namespace NWorldTest
{
	// 定義、JSON、hash、validation、同一材質判定と履歴を純粋に確認する。
	void RunMeshMaterialEffectDefinitionTests( NAssetTest::FChecker& C );
	// 保存・再生でメッシュ材質の時間変化を確認する。
	void RunMeshMaterialPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 偽の資産backendで材質用画像の準備と保持を確認する。
	void RunMeshMaterialResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir );
	// 実描画でメッシュ材質の結果を確認する。
	void RunMeshMaterialRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// 実エディター入力、保存、Undo/Redoを確認する。
	void RunWorldMeshMaterialEditorTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& TestDirectory );
}

#endif
