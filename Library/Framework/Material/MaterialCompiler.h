// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/MaterialDefinition.h"
#include "Library/Framework/Material/MaterialProgram.h"

//=============================================================================
// NMaterialCompiler
//   マテリアルの定義（グラフ）から、中間表現（FMaterialProgram）を作る。型の検査・ドメインの検査・輪の検出・出力ピンの検査・パラメーターと画像の置き場の割り当てをする。
//   失敗しても、止まらずに、見つかった問題をすべて Diagnostics に入れる（ノードの番号つき。エディタが、そのノードへジャンプできる）。エラーが 1 つでもあれば、bValid は false。
//
//   - 型: 算術は、同じ型どうし、または、Float（1 成分）と、ほかの型（Float が、広がる）。成分の数が違うベクトルどうし（Float2 と Float3 など）は、エラー。
//   - ピンに求められる型（UV は Float2、色は Float3 など）より大きいベクトルは、先頭の成分に切り詰める（Float4 から Float3 へは、知らせなし。それ以外は、注意）。小さいベクトルは、エラー。
//   - バックエンドが持たない能力は、簡略版へ落とし、Info の診断で知らせる（FMaterialCapabilities）。画像の数やパラメーターの数が上限を超えたら、エラー。
//   - グラフの出力から、たどれないノードは、無視する（コンパイルされない）。
//   - HLSL の直指定（CustomHlsl）のマテリアルは、Parameters から、置き場だけを作る（Code は空）。
//=============================================================================
namespace NMaterialCompiler
{
	FMaterialProgram Compile( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities = NMaterial::DefaultCapabilities() );
}
