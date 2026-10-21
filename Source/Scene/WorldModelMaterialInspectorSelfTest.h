// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "AssetSelfTestSupport.h"

class CAssetManager;
class IDrawExecutor;

namespace NWorldTest
{
	// fake backendとEditorAssetInfoの静的面cache・所有寿命を検査する。
	void RunWorldModelInspectorFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory );
	// 実モデルのInspector編集・再生経路を検査する。
	void RunWorldModelInspectorEditorChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory );
}
