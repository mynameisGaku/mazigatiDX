// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_SOURCE_SCENE_WORLDMODELPREVIEWSELFTEST_H
#define FRAMEWORK_SOURCE_SCENE_WORLDMODELPREVIEWSELFTEST_H

#include "AssetSelfTestSupport.h"

class CAssetManager;
class IDrawExecutor;

// ワールドエディタのモデル効果preview ownerを確認する検査。
namespace NWorldTest
{
	// 偽のbackendで個体cache、時計、fallback、解放を確かめる。
	void RunWorldModelPreviewFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory );
	// 実backendでviewport経由の効果描画とnative復帰を確かめる。
	void RunWorldModelPreviewRealChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory );
}

#endif
