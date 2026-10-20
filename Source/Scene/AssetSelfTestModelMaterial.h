// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_ASSETSELFTESTMODELMATERIAL_H
#define SOURCE_SCENE_ASSETSELFTESTMODELMATERIAL_H

#include "Library/Core/Collection/String.h"

class CAssetManager;
class IDrawExecutor;

namespace NAssetTest
{
	struct FChecker;
	class FFakeAssetBackend;

	// Core commandとchunk上限・index順・失敗時出力保持を確認する。
	void RunStaticModelMaterialCoreChecks( FChecker& Checker );
	// fake backendの材質別geometry copyと解放を確認する。
	void RunStaticModelMaterialFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory );
	// DirectX model instanceの個体別材質効果と画素を確認する。
	void RunStaticModelMaterialRealChecks( FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory );
}

#endif
