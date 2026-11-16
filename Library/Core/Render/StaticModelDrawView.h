// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawVertex3D.h"
#include "Library/Core/Memory/SharedPtr.h"

// 静的モデルの連続した材質範囲。画像の番号はモデルが所有する借用値。
struct FStaticModelDrawSection
{
	int32 FirstIndex = 0; // index列の先頭。
	int32 NumIndices = 0; // 三角形のindex数。
	int32 Texture = -1; // 拡散画像。負なら画像なし。
};

// 不変形状と個体の不透明度。VertexOwner があればビューのコピーも形状の寿命を保持する。
struct FStaticModelDrawView
{
	const FDrawVertex3D* Vertices = nullptr; // フレーム変換済み・モデル配置前の頂点。
	int32 NumVertices = 0; // 頂点数。
	const uint32* Indices = nullptr; // 三角形の参照順。
	int32 NumIndices = 0; // index数。
	const FStaticModelDrawSection* Sections = nullptr; // 全indexを隙間なく覆う材質範囲。
	int32 NumSections = 0; // 範囲の数。
	float32 Opacity = 1; // 個体ごとの不透明度。
	uint64 MeshKey = 0; // 頂点の置き場（GPU の共有メッシュ）の識別。0 なら、GPU に置いて使い回さない。
	uint64 MeshVersion = 0; // 頂点の中身の版（変わったら、置き直す）。
	TSharedPtr<const FDrawVertex3D> VertexOwner; // 頂点へ別名参照し、形状と所有画像を描画中保持する。空なら従来の借用。
};
