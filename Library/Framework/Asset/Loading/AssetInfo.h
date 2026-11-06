// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Asset.h"

// 読み込んでいるアセット 1 つの情報（デバッグ表示用）
struct FAssetInfo
{
	EAssetType Type = EAssetType::Texture;
	FString Path;
	FString Description;
	int32 RefCount = 0;   // 使っている TSharedPtr の数（アセットの管理役自身は数えない）
	bool bReady = true;
	int64 MemoryBytes = 0;   // 使っているメモリの推定
	int64 FileBytes = 0;     // ファイルの大きさ
	float32 LoadMs = 0.0f;   // 読み込みにかかった時間
};
