// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector4.h"

//=============================================================================
// Surface のマテリアルの、定数バッファの中身（HLSL の cbScene・cbObject と、同じ並び。MaterialHlsl.cpp の SurfaceDeclarations）
//   マテリアル（CMaterial）と、影のマップ（CMaterialShadowMap）が、同じ頂点シェーダーへ渡す。
//=============================================================================

// HLSL の cbScene
struct FMaterialSceneConstants
{
	FMatrix4x4 ViewProjection = FMatrix4x4::Identity;
	FVector4 LightDirection = FVector4( 0.0f, -1.0f, 0.0f, 0.0f );
	FVector4 CameraPosition = FVector4( 0.0f, 0.0f, 0.0f, 1.0f );
	FVector4 Time = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FVector4 LightColor = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );        // rgb = 平行光の色、w = 点光源の数
	FVector4 SkyColor = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FVector4 GroundColor = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FVector4 AmbientColor = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FVector4 Environment = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );       // 環境マップの 1 段の幅・高さ・段の数・明るさの倍率
	FVector4 PointLightPositions[4];                                  // xyz = 位置、w = 半径
	FVector4 PointLightColors[4];
	FMatrix4x4 ShadowViewProjection = FMatrix4x4::Identity;
	FVector4 ShadowOptions = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );     // 影のマップの大きさ・深度のずらし・法線の向きのずらし
};

// HLSL の cbObject
struct FMaterialObjectConstants
{
	FMatrix4x4 World = FMatrix4x4::Identity;
	FVector4 Color = FVector4( 1.0f, 1.0f, 1.0f, 1.0f );
	FVector4 Options = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
};
