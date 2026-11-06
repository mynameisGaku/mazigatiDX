// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/Definition/WorldDefinition.h"

//=============================================================================
// ワールドの中での、アセットの使われ方
//   「このエフェクト（アニメーションセット・モデル・音・画像・入れ子のワールド）は、このワールドの、どのオブジェクトが使っているか」を調べる純粋な関数。
//   エディタの「Used by」の表示や、アセットを消す・改名する前の確認に使う（ファイルは読まない。FWorldDefinition だけを見る）。
//   パスは NWorld::NormalizeAssetPath で比べる（区切りと大文字小文字の違いは、同じものとして扱う）。
//=============================================================================
struct FWorldAssetUse
{
	int32 ObjectId = 0;
	FString ObjectName;
	// どの部品の、どの項目か（"Model" / "Model animations" / "Billboard" / "Sound" / "Effect" / "Collider mesh" / "Prefab"）
	FString Component;
};

namespace NWorld
{
	// Definition の中で、AssetPath を使っているところを、オブジェクトの並びの順に返す。パスが空なら、空の配列
	TArray<FWorldAssetUse> FindAssetUses( const FWorldDefinition& Definition, const FString& AssetPath );
}
