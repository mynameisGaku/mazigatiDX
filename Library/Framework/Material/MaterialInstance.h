// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/Material.h"

class CAssetSubsystem;

//=============================================================================
// FMaterialInstanceDefinition（.gxmaterialinstance）
//   親のマテリアルと、パラメーターの上書き。コンパイルし直さずに、見た目違いを大量に作れる（CMaterial::CreateInstance）。
//
//   { "Format": "gxmaterialinstance", "Version": 1, "Parent": "Engine:/Materials/UI/GlowButton.gxmaterial",
//     "Overrides": { "Speed": 2.0, "Tint": [1, 0.2, 0.2, 1], "Mask": "Textures/Mask.png" } }
//
//   上書きの値: 数 = Float / 配列 = Float2〜Float4 / 文字列 = 画像のパス
//=============================================================================
struct FMaterialInstanceDefinition
{
	FString ParentPath;
	TArray<FMaterialParameterDecl> Overrides;   // Float（Value.x）・Float2〜4・Texture（Path）

	FString ToJson() const;
	static bool FromJson( const FString& Text, FMaterialInstanceDefinition& OutDefinition, FString* OutError = nullptr );

	// 上書きを、インスタンスへ適用する（画像は、Assets があれば読み込む）。適用できなかった名前は、OutMissing へ（null でもよい）
	void Apply( CMaterial& Instance, CAssetSubsystem* Assets, TArray<FString>* OutMissing = nullptr ) const;
};
