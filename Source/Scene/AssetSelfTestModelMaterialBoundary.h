// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_ASSETSELFTESTMODELMATERIALBOUNDARY_H
#define SOURCE_SCENE_ASSETSELFTESTMODELMATERIALBOUNDARY_H

class CAssetManager;
namespace NAssetTest { struct FChecker; class FFakeAssetBackend; }
class FString;

namespace NAssetTest
{
	// Fake backendで長時間時計・個体別資源解放の境界を確認する。
	void RunStaticModelMaterialBoundaryFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory );
	// DxLib backendで選択材質geometryの失敗原子性と多chunk境界を確認する。
	void RunStaticModelMaterialBoundaryRealChecks( FChecker& Checker, CAssetManager& Assets, const FString& Directory );
}

#endif
