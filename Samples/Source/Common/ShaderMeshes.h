// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/ShaderVertex.h"

//=============================================================================
// NShadersMesh
//   サンプル Shaders で、シェーダーに渡す形（頂点と添字）。一度だけ作って、毎フレーム使い回す。
//   どれも、PrimitiveIndexedShader3D に渡す（EDrawPrimitive::Triangles）。頂点シェーダーが、自分の行列で動かす。
//=============================================================================
namespace NShadersMesh
{
	// 頂点と添字の入れ物
	struct FMesh
	{
		TArray<FShaderVertex3D> Vertices;
		TArray<uint16> Indices;
	};

	// 半径 1 の球（中心が原点。経度 24 x 緯度 16 の分割）
	const FMesh& GetSphere();

	// 一辺が 1 の立方体（中心が原点。面ごとの法線つき）
	const FMesh& GetCube();

	// 地面（y = 0 の水平な四角形。x と z は -GroundHalfSize〜GroundHalfSize。法線は上）
	const FMesh& GetGround();

	// 地面の広さ（片側。ワールドの単位）
	constexpr float32 GroundHalfSize = 40.0f;
}
