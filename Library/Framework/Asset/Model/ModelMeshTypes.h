// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// モデルのメッシュの中身（頂点と三角形）の値の型
//   IAssetBackend::GetModelMeshData が埋め、CModelInstance::GetMeshData が返す。
//   座標はモデルのローカル座標（モデルの置き場所は含まない）。番号（Index）は FModelStructure と同じ並び。-1 は「無い」。
//=============================================================================

// 頂点 1 つ
struct FModelMeshVertex
{
	FVector3 Position;
	FVector3 Normal = FVector3( 0.0f, 1.0f, 0.0f );
	// 1 枚目のテクスチャ座標
	FVector2 UV;
	// 頂点の色（0〜1）
	FColor Color = FColor( 1.0f, 1.0f, 1.0f, 1.0f );
	// いちばん強く影響する骨（フレーム）の番号。骨に影響されない頂点は -1
	int32 DominantFrame = -1;
};

// 三角形 1 つ
struct FModelMeshTriangle
{
	// 頂点の番号（FModelMeshData::Vertices の中の番号）
	int32 Vertices[3] = { -1, -1, -1 };
	// この三角形を持つメッシュ・使う材質・持ち主のフレームの番号
	int32 Mesh = -1;
	int32 Material = -1;
	int32 Frame = -1;
};

// メッシュの中身の全体（頂点の並びは三角形が共有する）
struct FModelMeshData
{
	TArray<FModelMeshVertex> Vertices;
	TArray<FModelMeshTriangle> Triangles;
	// 頂点の範囲
	FVector3 BoundsMin;
	FVector3 BoundsMax;
};
