// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWDISTORTEDMATERIALQUAD_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWDISTORTEDMATERIALQUAD_H

#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"

// 画像領域と独立したノイズ画像で、材質四角形の表示UVを曲げる値。
struct FDrawDistortedMaterialQuadData
{
	// 消失・発光・元画像を含む既存材質設定。
	FDrawMaterialQuadData Material;
	// UVのゆがみ方向を読む画像。
	int32 DistortionTexture = -1;
	// ずれ幅。0から1の範囲で指定する。
	float32 Strength = 0.0f;
	// ゆがみ画像へ掛ける横・縦倍率。
	float32 UVScale[2] = { 1.0f, 1.0f };
	// ゆがみ画像へ加える、折返し済みのUV移動。
	float32 UVOffset[2] = {};
};

namespace NDrawDistortedMaterialQuad
{
	// 既存材質設定と変位画像の指定が描画可能な値かを調べる。
	inline bool IsValid( const FDrawDistortedMaterialQuadData& Data )
	{
		if ( !NDrawMaterialQuad::IsValid( Data.Material ) || Data.DistortionTexture < 0 || !std::isfinite( Data.Strength ) || Data.Strength < 0.0f || Data.Strength > 1.0f ) return false;
		if ( !std::isfinite( Data.UVScale[0] ) || !std::isfinite( Data.UVScale[1] ) || Data.UVScale[0] <= 0.0f || Data.UVScale[1] <= 0.0f || Data.UVScale[0] > 128.0f || Data.UVScale[1] > 128.0f ) return false;
		return std::isfinite( Data.UVOffset[0] ) && std::isfinite( Data.UVOffset[1] ) && Data.UVOffset[0] >= 0.0f && Data.UVOffset[0] < 1.0f && Data.UVOffset[1] >= 0.0f && Data.UVOffset[1] < 1.0f;
	}
}

static_assert( sizeof( FDrawDistortedMaterialQuadData ) % 4 == 0 && std::is_trivially_copyable_v<FDrawDistortedMaterialQuadData>, "FDrawDistortedMaterialQuadData must be 4-byte words" );

#endif
