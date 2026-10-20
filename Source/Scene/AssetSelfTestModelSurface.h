// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_ASSETSELFTESTMODELSURFACE_H
#define SOURCE_SCENE_ASSETSELFTESTMODELSURFACE_H

#include "Library/Core/Collection/String.h"

class CAssetManager;

namespace NAssetTest
{
	struct FChecker;
	class FFakeAssetBackend;

	// .x fixtureと借用diffuse画像を同じ内容で指定名へ書き出す。
	bool WriteStaticModelSurfaceFixture( const FString& Directory, const FString& ModelFileName );
	// fake backendでsnapshotの失敗・値の不変性・元モデル寿命を確認する。
	void RunStaticModelSurfaceFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory );
	// DxLib backendで静的面値の取得・借用画像寿命・未対応モデルを確認する。
	void RunStaticModelSurfaceRealChecks( FChecker& Checker, CAssetManager& Assets, const FString& Directory );
}

#endif
