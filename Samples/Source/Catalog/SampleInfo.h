// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

class CSceneManager;

//=============================================================================
// FSampleInfo
//   サンプル 1 つの情報。一覧（ブラウザー）に出す名前・説明と、シーンの登録のしかたをまとめる。
//   サンプルは、自分の .cpp の中で TSampleRegistrar を使って、これを渡して登録する（Catalog/SampleCatalog.h を参照）。
//=============================================================================
struct FSampleInfo
{
	// シーンの名前。--sample=Name で直接起動するときの名前でもある。英字で始め、英数字と _ だけにする（ほかのサンプルと重ねない）
	FString Name;

	// 一覧に出す名前
	FString Title;

	// 一覧に出す説明（1 行）
	FString Description;

	// 一覧の並び。小さいものが上で、同じなら Name の順
	int32 Order = 0;

	// シーンマネージャーへ、このサンプルのシーンを Name で登録する関数（CSampleCatalog::Add が設定するので、自分では書かない）
	void ( *RegisterScene )( CSceneManager& Scenes, const FString& Name ) = nullptr;
};
