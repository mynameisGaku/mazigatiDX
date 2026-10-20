#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// メッシュの頂点（プラットフォームに依らない形）
//   編集メッシュ（Framework）が作り、バックエンド（IAssetBackend）が頂点バッファにする。描画コマンド（Mesh3D）は、そのバッファのハンドルを持つ。
//   すべてのメンバが 4 バイトの倍数で、隙間がない（36 バイト）。DxLib の VERTEX3D に変換して渡す。
//   材質の色は、頂点の色（Color）に入れる（画像があれば、画像に掛かる）。DxLib の SetDrawBright は、頂点バッファの 3D 描画には効かないため、描画のときには変えられない。
//=============================================================================
struct FMeshVertex
{
	float32 Position[3] = {};
	float32 Normal[3] = { 0.0f, 1.0f, 0.0f };
	float32 U = 0.0f;
	float32 V = 0.0f;
	uint8 Color[4] = { 255, 255, 255, 255 };   // R, G, B, A
};

static_assert( sizeof( FMeshVertex ) == 36, "FMeshVertex must have no padding" );
