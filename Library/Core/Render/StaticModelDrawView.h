// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawVertex3D.h"

// 静的モデルの連続した材質範囲。画像の番号はモデルが所有する借用値。
struct FStaticModelDrawSection
{
	int32 FirstIndex = 0; // index列の先頭。
	int32 NumIndices = 0; // 三角形のindex数。
	int32 Texture = -1; // 拡散画像。負なら画像なし。
};

// 同期の命令解釈中だけ借用する不変形状。個体の不透明度は値で固定する。
struct FStaticModelDrawView
{
	const FDrawVertex3D* Vertices = nullptr; // フレーム変換済み・モデル配置前の頂点。
	int32 NumVertices = 0; // 頂点数。
	const uint32* Indices = nullptr; // 三角形の参照順。
	int32 NumIndices = 0; // index数。
	const FStaticModelDrawSection* Sections = nullptr; // 全indexを隙間なく覆う材質範囲。
	int32 NumSections = 0; // 範囲の数。
	float32 Opacity = 1; // 個体ごとの不透明度。
};
