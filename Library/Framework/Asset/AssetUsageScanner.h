// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/Definition/WorldAssetUse.h"

class CAssetSubsystem;

//=============================================================================
// アセットの使われ方の走査
//   アセットのルートの下のすべての .gxworld を読み、AssetPath を使っているワールドと、その使い方を返す（使っていないワールドは入れない）。
//   走査のために読んだワールドは、使い終えると（参照が無くなると）解放される（メモリに残らない）。
//   時間がかかりうる（ワールドの数だけ読む）ので、毎フレームは呼ばない。ゲームスレッドから呼ぶ。
//=============================================================================
struct FWorldUsage
{
	FString WorldPath;                    // アセットのルートからの相対（区切りは /）
	TArray<FWorldAssetUse> Uses;
};

namespace NAssetUsage
{
	TArray<FWorldUsage> FindWorldsUsing( CAssetSubsystem& Assets, const FString& AssetPath );
}
