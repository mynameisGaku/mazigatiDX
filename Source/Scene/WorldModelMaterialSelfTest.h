// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_WORLDMODELMATERIALEFFECTSELFTEST_H
#define SOURCE_SCENE_WORLDMODELMATERIALEFFECTSELFTEST_H

#include "Library/Core/Collection/String.h"

class CAssetManager;
class CScene;
class IDrawExecutor;

namespace NAssetTest
{
	struct FChecker;
	class FFakeAssetBackend;
}

namespace NWorldTest
{
	// 保存値・不正marker・履歴・world validationをscene非依存で調べる。
	void RunWorldModelMaterialDefinitionTests( NAssetTest::FChecker& Checker );
	// fake backendでworld spawn、fallback、時計、resource照会回数と解放を調べる。
	void RunWorldModelMaterialSpawnFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory );
	// 実backendで保存再読後のspawn、ECS更新、ModelMaterial command選択を調べる。
	void RunWorldModelMaterialSpawnRealChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory );
}

#endif
