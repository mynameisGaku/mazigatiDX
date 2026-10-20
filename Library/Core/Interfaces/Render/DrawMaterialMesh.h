// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALMESH_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALMESH_H

#include <cmath>
#include <type_traits>

#include "DrawMaterialRibbon.h"
#include "MeshTypes.h"

// 固定済みワールド頂点と材質設定を所有コピーする三角形命令。
struct FDrawMaterialMeshData
{
	// 描画画像のhandle。無効な負値は記録しない。
	int32 Texture = -1;
	// 後続payloadに含める頂点数。
	int32 NumVertices = 0;
	// 後続payloadに含める三角形index数。
	int32 NumIndices = 0;
	// 画像マスク・消失・境界発光を共有する設定。
	FDrawMaterialRibbonData::FMaterialSettings Material;
	// 元画像へ加える未折返しのUV移動量。
	float32 UVOffset[2] = {};
	// 消失マスクのしきい値。
	float32 Dissolve = 0.0f;
	// 元画像へ掛ける自己発光倍率。
	float32 Emission = 1.0f;
	// ゆがみ画像handle。強度0なら-1を許可する。
	int32 DistortionTexture = -1;
	// 頂点間で補間するUV変位強度。
	float32 DistortionStrength = 0.0f;
	// ゆがみ画像へ掛ける横・縦倍率。
	float32 DistortionUVScale[2] = { 1.0f, 1.0f };
	// ゆがみ画像へ加える未折返し移動量。
	float32 DistortionUVOffset[2] = {};
	// 0は呼出側の裏面除去を継続し、1は裏面除去を無効にする。
	int32 bTwoSided = 1;
	// 一命令に含める頂点の最大数。
	static constexpr int32 MaxVertices = 1024;
	// 一命令に含める三角形indexの最大数。
	static constexpr int32 MaxIndices = 3072;
};

static_assert( sizeof( FDrawMaterialMeshData ) == 96 && sizeof( FDrawMaterialMeshData ) % 4 == 0 && std::is_trivially_copyable_v<FDrawMaterialMeshData>, "material mesh header must occupy twenty-four words" );
static_assert( sizeof( FMeshVertex ) == 36 && std::is_trivially_copyable_v<FMeshVertex>, "material mesh vertex must occupy nine words" );

namespace NDrawMaterialMesh
{
	// 固定設定と配列数が命令上限内かを調べ、範囲外・非有限値ならfalseを返す。
	inline bool IsValidHeader( const FDrawMaterialMeshData& Data )
	{
		// 材質の固定設定。
		const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
		if ( Data.Texture < 0 || Data.NumVertices < 3 || Data.NumVertices > FDrawMaterialMeshData::MaxVertices || Data.NumIndices < 3 || Data.NumIndices > FDrawMaterialMeshData::MaxIndices || Data.NumIndices % 3 != 0 || Data.DistortionTexture < -1 || ( Data.bTwoSided != 0 && Data.bTwoSided != 1 ) ) return false;
		if ( !std::isfinite( Material.UVScale[0] ) || !std::isfinite( Material.UVScale[1] ) || Material.UVScale[0] <= 0.0f || Material.UVScale[1] <= 0.0f || Material.UVScale[0] > 128.0f || Material.UVScale[1] > 128.0f || !std::isfinite( Material.MaskScale ) || Material.MaskScale <= 0.0f || Material.MaskScale > 128.0f || !std::isfinite( Material.EdgeWidth ) || Material.EdgeWidth < 0.0f || Material.EdgeWidth > 1.0f || !std::isfinite( Material.EdgeEmission ) || Material.EdgeEmission < 0.0f || Material.EdgeEmission > 32.0f || ( Material.bDissolve != 0 && Material.bDissolve != 1 ) || ( Material.DepthTest != 0 && Material.DepthTest != 1 ) || ( Material.DepthWrite != 0 && Material.DepthWrite != 1 ) || Material.MaskTexture < -1 ) return false;
		if ( !std::isfinite( Data.UVOffset[0] ) || !std::isfinite( Data.UVOffset[1] ) || std::abs( Data.UVOffset[0] ) > 1.0e6f || std::abs( Data.UVOffset[1] ) > 1.0e6f || !std::isfinite( Data.Dissolve ) || Data.Dissolve < 0.0f || Data.Dissolve > 1.0f || !std::isfinite( Data.Emission ) || Data.Emission < 0.0f || Data.Emission > 32.0f ) return false;
		if ( !std::isfinite( Data.DistortionStrength ) || Data.DistortionStrength < 0.0f || Data.DistortionStrength > 1.0f || ( Data.DistortionStrength > 0.0f && Data.DistortionTexture < 0 ) ) return false;
		return std::isfinite( Data.DistortionUVScale[0] ) && std::isfinite( Data.DistortionUVScale[1] ) && Data.DistortionUVScale[0] > 0.0f && Data.DistortionUVScale[1] > 0.0f && Data.DistortionUVScale[0] <= 128.0f && Data.DistortionUVScale[1] <= 128.0f && std::isfinite( Data.DistortionUVOffset[0] ) && std::isfinite( Data.DistortionUVOffset[1] ) && std::abs( Data.DistortionUVOffset[0] ) <= 1.0e6f && std::abs( Data.DistortionUVOffset[1] ) <= 1.0e6f;
	}

	// 全頂点・全indexが記録可能か調べ、無効値が一つでもあればfalseを返す。
	// Data: 検証済みの固定設定と必要な配列数。
	// Vertices: NumVertices個以上の有効な型付き頂点配列。
	// Indices: NumIndices個以上の有効な型付きindex配列。
	inline bool IsValid( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices )
	{
		if ( Vertices == nullptr || Indices == nullptr || !IsValidHeader( Data ) ) return false;
		// 点列全体で使用するUV倍率と移動量。
		const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
		// 有限性と上限を調べる頂点添字。
		for ( int32 Index = 0; Index < Data.NumVertices; ++Index )
		{
			// 位置・法線・UVを持つ現在頂点。
			const FMeshVertex& Vertex = Vertices[Index];
			if ( !std::isfinite( Vertex.Position[0] ) || !std::isfinite( Vertex.Position[1] ) || !std::isfinite( Vertex.Position[2] ) || !std::isfinite( Vertex.Normal[0] ) || !std::isfinite( Vertex.Normal[1] ) || !std::isfinite( Vertex.Normal[2] ) || !std::isfinite( Vertex.U ) || !std::isfinite( Vertex.V ) || std::abs( Vertex.U ) > 1.0e6f || std::abs( Vertex.V ) > 1.0e6f ) return false;
			if ( !std::isfinite( Vertex.U * Material.UVScale[0] + Data.UVOffset[0] ) || !std::isfinite( Vertex.V * Material.UVScale[1] + Data.UVOffset[1] ) || !std::isfinite( Vertex.U * Material.MaskScale ) || !std::isfinite( Vertex.V * Material.MaskScale ) || !std::isfinite( Vertex.U * Data.DistortionUVScale[0] + Data.DistortionUVOffset[0] ) || !std::isfinite( Vertex.V * Data.DistortionUVScale[1] + Data.DistortionUVOffset[1] ) ) return false;
		}
		// 頂点配列範囲を超えないindex添字。
		for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
		{
			// 三角形が参照する頂点番号。
			if ( Indices[Index] >= static_cast<uint32>( Data.NumVertices ) ) return false;
		}
		return true;
	}
}

#endif
