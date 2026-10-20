// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_SOURCE_SCENE_WORLDEFFECTMESHRESOURCESELFTEST_H
#define FRAMEWORK_SOURCE_SCENE_WORLDEFFECTMESHRESOURCESELFTEST_H

#include "AssetSelfTestSupport.h"

class CAssetManager;

namespace NWorldTest
{
	// fake backend上でmesh材質資源の準備、再生snapshot、解放を確認する。
	// C: 検査結果を書き込むchecker。Assets: 対象sandboxを使うasset manager。
	// Fake: 現在設定中のbackend。Dir: fixtureの保存先。fixture作成に失敗した項目はcheckerへ記録する。
	void RunEffectMeshResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir );
}

#endif
