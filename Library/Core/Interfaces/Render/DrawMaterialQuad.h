// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALQUAD_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALQUAD_H

#include "DrawCommand.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"

// 画像と任意のマスクを使い、消失・境界発光・UV移動を指定する値。
struct FDrawMaterialQuadData
{
	// 既存の四角形の向き・寸法・画像情報。
	FDrawQuad3DData Quad;
	// 消失マスク画像のハンドル。-1なら組み込み模様を使う。
	int32 MaskTexture = -1;
	// 表示画像へ掛けるUV倍率。
	float32 UVScale[2] = { 1.0f, 1.0f };
	// 表示画像へ加えるUV移動量。
	float32 UVOffset[2] = {};
	// マスク画像へ掛けるUV倍率。
	float32 MaskScale = 1.0f;
	// マスクの消失しきい値。
	float32 Dissolve = 0.0f;
	// 表示画像の自己発光倍率。
	float32 Emission = 1.0f;
	// 消失境界の幅。0なら境界を描かない。
	float32 EdgeWidth = 0.08f;
	// 消失境界の発光倍率。
	float32 EdgeEmission = 2.0f;
	// 消失境界へ加える色。
	FDrawRgba EdgeColor = { 255, 128, 32, 255 };
	// 0なら消失と境界発光を無効にする。
	int32 bDissolve = 0;
	// 深度を比較して手前の物体に隠されるか。比較方法はLessEqual。
	int32 DepthTest = 1;
	// 深度を書き込んで後続描画を隠すか。半透明描画では通常0。
	int32 DepthWrite = 0;
};

namespace NDrawMaterialQuad
{
	// 四角形と材質値がGPUへ安全に渡せる有限範囲かを調べる。
	inline bool IsValid( const FDrawMaterialQuadData& Data )
	{
		// 検証対象となる四角形の形状・画像座標。
		const FDrawQuad3DData& Quad = Data.Quad;
		// 三成分の位置・方向に非有限値がないか調べる。
		const auto Finite3 = []( const float32 ( &Values )[3] ) { return NMath::IsFinite( Values[0] ) && NMath::IsFinite( Values[1] ) && NMath::IsFinite( Values[2] ); };
		if ( Quad.Handle < 0 || Data.MaskTexture < -1 || !Finite3( Quad.Pos ) || !Finite3( Quad.Right ) || !Finite3( Quad.Up ) || !NMath::IsFinite( Quad.Width ) || !NMath::IsFinite( Quad.Height ) || Quad.Width <= 0.0f || Quad.Height <= 0.0f || Quad.Width > 1.0e6f || Quad.Height > 1.0e6f ) return false;
		if ( !NMath::IsFinite( Quad.PivotX ) || !NMath::IsFinite( Quad.PivotY ) || Quad.PivotX < 0.0f || Quad.PivotX > 1.0f || Quad.PivotY < 0.0f || Quad.PivotY > 1.0f || !NMath::IsFinite( Quad.Angle ) || !NMath::IsFinite( Quad.U0 ) || !NMath::IsFinite( Quad.V0 ) || !NMath::IsFinite( Quad.U1 ) || !NMath::IsFinite( Quad.V1 ) || Quad.U0 < 0.0f || Quad.V0 < 0.0f || Quad.U1 > 1.0f || Quad.V1 > 1.0f || Quad.U0 >= Quad.U1 || Quad.V0 >= Quad.V1 ) return false;
		if ( Quad.Orient < static_cast<int32>( EDrawQuadOrient::Camera ) || Quad.Orient > static_cast<int32>( EDrawQuadOrient::Fixed ) ) return false;
		if ( !NMath::IsFinite( Data.UVScale[0] ) || !NMath::IsFinite( Data.UVScale[1] ) || !NMath::IsFinite( Data.UVOffset[0] ) || !NMath::IsFinite( Data.UVOffset[1] ) || NMath::Abs( Data.UVScale[0] ) > 1.0e4f || NMath::Abs( Data.UVScale[1] ) > 1.0e4f || NMath::Abs( Data.UVOffset[0] ) > 1.0e6f || NMath::Abs( Data.UVOffset[1] ) > 1.0e6f ) return false;
		return NMath::IsFinite( Data.MaskScale ) && Data.MaskScale > 0.0f && Data.MaskScale <= 1.0e4f && NMath::IsFinite( Data.Dissolve ) && Data.Dissolve >= 0.0f && Data.Dissolve <= 1.0f && NMath::IsFinite( Data.Emission ) && Data.Emission >= 0.0f && Data.Emission <= 64.0f && NMath::IsFinite( Data.EdgeWidth ) && Data.EdgeWidth >= 0.0f && Data.EdgeWidth <= 1.0f && NMath::IsFinite( Data.EdgeEmission ) && Data.EdgeEmission >= 0.0f && Data.EdgeEmission <= 64.0f && ( Data.bDissolve == 0 || Data.bDissolve == 1 ) && ( Data.DepthTest == 0 || Data.DepthTest == 1 ) && ( Data.DepthWrite == 0 || Data.DepthWrite == 1 );
	}
}

static_assert( sizeof( FDrawMaterialQuadData ) % 4 == 0 && std::is_trivially_copyable_v<FDrawMaterialQuadData>, "FDrawMaterialQuadData must be 4-byte words" );

#endif
