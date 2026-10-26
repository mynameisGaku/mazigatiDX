// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Collection/String.h"

namespace NAssetTest
{
	// 確認結果を収集する自己検査の判定役。
	struct FChecker;
}

// 実アセット読込テストが借りるasset管理役。
class CAssetManager;

namespace NWorldTest
{
	// World の Effect 部品の動的終点指定（JSON・検証・複製と貼り付けの Id 付け替え）を、アセットなしで確かめる。
	void RunEffectTargetWorldTests( NAssetTest::FChecker& C );
	// 配置時の対象オブジェクト接続と、Effect Editor の試し再生の終点（有効化・再生への反映・クリック配置・カメラ・ガイド）を実アセットで確かめる。
	void RunEffectTargetSceneEditorTests( NAssetTest::FChecker& C, CAssetManager& Assets );
}
