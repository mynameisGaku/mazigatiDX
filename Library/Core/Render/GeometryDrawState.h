// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 頂点をリスト形式でつなぐ方法。帯・扇は共通処理でリストへ展開する。
enum class EGeometryTopology : uint8
{
	Points, // 1頂点ずつの点。
	Lines, // 2頂点ずつの線分。
	Triangles, // 3頂点ずつの三角形。
};

// 描く深度と、描画先の深度との比較。
enum class EGeometryCompare : uint8
{
	Never, // 常に拒否。
	Less, // 小さい場合。
	Equal, // 等しい場合。
	LessEqual, // 以下の場合。
	Greater, // 大きい場合。
	NotEqual, // 異なる場合。
	GreaterEqual, // 以上の場合。
	Always, // 常に通す。
};

// 表は画面上で時計回り。描画API固有の表裏定義は実装側で合わせる。
enum class EGeometryCulling : uint8
{
	None, // 両面。
	BackFaces, // 裏面を除去。
	FrontFaces, // 表面を除去。
};

// 1回の描画に閉じたGPU状態。ほかの命令へ持ち越さない。
struct FGeometryDrawState
{
	EGeometryTopology Topology = EGeometryTopology::Triangles; // 頂点のつなぎ方。
	EGeometryCompare Compare = EGeometryCompare::LessEqual; // 深度の比較。
	EGeometryCulling Culling = EGeometryCulling::None; // 除去する面。
	bool bTestDepth = true; // 深度を比べるか。
	bool bWriteDepth = true; // 通過した深度を書くか。
	bool bAlphaBlend = false; // 通常の非乗算済みアルファ合成か。
	int32 DepthBias = 0; // GPUの整数深度偏り。
};
