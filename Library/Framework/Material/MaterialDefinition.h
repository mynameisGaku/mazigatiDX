// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/MaterialGraph.h"

//=============================================================================
// FMaterialDefinition（.gxmaterial）
//   マテリアルの定義: ドメイン・陰影・グラフ（または、HLSL の直指定）。JSON で保存する。
//
//   {
//     "Format": "gxmaterial", "Version": 1,
//     "Name": "GlowButton", "Domain": "UI",
//     "Nodes": [ { "Id": 1, "Kind": "VectorParameter", "Name": "Tint", "Value": [1, 0.5, 0, 1], "Color": true, "Position": [0, 0], "Inputs": [] }, ... ],
//     "Outputs": { "Color": 3, "Opacity": 4 }
//   }
//
//   HLSL の直指定（グラフを使わない。バックエンド別の出口。移植性は、書いた人の責任）:
//     "CustomHlsl": "...ピクセルシェーダーの全文...", "Parameters": [ { "Name": "Tint", "Type": "Float4", "Value": [1, 1, 1, 1] }, { "Name": "Tex", "Type": "Texture", "Path": "..." } ]
//     パラメーターは、宣言の順に g_Params[0]、g_Params[1] ... に入る（Float・Float2・Float3・Float4 は、どれも 4 つ組 1 つ）。画像（Texture）は、宣言の順に t0 から（PostProcess は、
//     t0 がシーンの色、深度を使えば t1 が深度で、そのあとに続く）。cbuffer と入力の構造体は、ドメインごとの決まり（MaterialHlsl.h）。
//=============================================================================
struct FMaterialParameterDecl
{
	FString Name;
	EMaterialValueType Type = EMaterialValueType::Float;   // Float〜Float4・Texture
	FVector4 Value = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FString Path;                                          // Texture: 既定の画像のパス
	bool bColor = false;
};

class FMaterialDefinition
{
public:
	FString Name;
	EMaterialDomain Domain = EMaterialDomain::UI;
	EMaterialShading Shading = EMaterialShading::Unlit;    // Surface だけ
	FMaterialGraph Graph;

	// HLSL の直指定（空でなければ、グラフの代わりに使う）
	FString CustomHlsl;
	TArray<FMaterialParameterDecl> Parameters;

	// JSON の文字列にする / JSON の文字列から読む（読めなければ false。OutError に、理由）
	FString ToJson() const;
	static bool FromJson( const FString& Text, FMaterialDefinition& OutDefinition, FString* OutError = nullptr );
};
