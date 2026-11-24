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
	int32 MaterialIndex = -1; // 元モデルの材質番号。不明な描画は負値。
};

// GPU スキニングで使える骨の行列の数の上限（EngineContent/Shaders/DX12/ForwardMesh.hlsl の g_Bones の大きさと同じ）。
constexpr int32 MaxStaticModelSkinBones = 256;

// GPU スキニング用の、頂点ごとの骨と重み（ForwardMesh.hlsl の SkinVertex と同じ並び。24 バイト）。重みの合計は 1。
struct FStaticModelSkinVertex
{
	uint16 Bones[4] = { 0, 0, 0, 0 };                    // 骨の行列の番号（FStaticModelDrawView::BoneMatrices の並び）。使わない枠は 0（重みも 0）。
	float32 Weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };     // 骨ごとの重み。
};
static_assert( sizeof( FStaticModelSkinVertex ) == 24, "FStaticModelSkinVertex must match the 24-byte SkinVertex of ForwardMesh.hlsl" );

// 不変形状と個体の不透明度。VertexOwner があればビューのコピーも形状の寿命を保持する。
// SkinVertices があるときは、GPU スキニングのビュー: Vertices は、骨の変形の前の頂点（基準の姿。形状を足したもの）で、骨の行列（BoneMatrices）で GPU が変形する。
// 頂点の版（MeshVersion）は、骨の動きでは変わらない（頂点・添字が変わるときだけ変わる）ので、GPU のメッシュは置いたまま使い回される。
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
	const FStaticModelSkinVertex* SkinVertices = nullptr; // GPU スキニング: Vertices と同じ数の、骨と重み。nullptr なら、Vertices は変形済み。
	const float32* BoneMatrices = nullptr;                // GPU スキニング: NumBones 個の骨の行列。1 つは、3 つの float4（行列の 1〜3 列。xyz = 回転拡大、w = 移動）。
	int32 NumBones = 0;                                   // 骨の行列の数（1 〜 MaxStaticModelSkinBones）。
	TSharedPtr<const float32> SkinOwner;                  // 骨の行列へ別名参照し、描画中保持する。
};
