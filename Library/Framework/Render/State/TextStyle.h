// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Color.h"

//=============================================================================
// FTextStyle
//   文字列の描き方の飾り（拡大・回転・縦書き・影）。FDrawContext::TextStyled / CFont::RecordStyled に渡す（DxLib の DrawRotaStringFToHandle に当たる）。
//
//   FTextStyle Style;
//   Style.ScaleX = Style.ScaleY = 2.0f;
//   Style.AngleRadians = 0.3f;
//   Style.AnchorX = 0.5f;                     // (X, Y) に、文字列の横の中央が来る。拡大・回転も、その点を中心にする
//   Font->RecordStyled( Draw, 320.0f, 100.0f, "Title", FColor32::White, Style );
//=============================================================================
struct FTextStyle
{
	// 縁取りのあるフォントの、縁の色
	FColor32 EdgeColor = FColor32( 0, 0, 0 );

	// 拡大率（横・縦。0 より大きい）
	float32 ScaleX = 1.0f;
	float32 ScaleY = 1.0f;

	// 回転（ラジアン。正の向きは時計回り）
	float32 AngleRadians = 0.0f;

	// 基準点: 文字列の大きさに対する割合（0 で左上、1 で右下。範囲の外でもよい）。描く位置 (X, Y) に、この点が来て、拡大と回転も、この点を中心にする。
	// 縦書きでは、(0, 0) が列の右上で、AnchorX は左へ増える（列は、右から左へ進む）
	float32 AnchorX = 0.0f;
	float32 AnchorY = 0.0f;

	// 縦書きにする（1 文字ずつ、上から下へ並べる。改行で、左の列へ進む）
	bool bVertical = false;

	// 影をつける: 同じ文字列を、(ShadowOffsetX, ShadowOffsetY)（画面のピクセル。回転しない）だけずらして、ShadowColor で、先に描く
	bool bShadow = false;
	FColor32 ShadowColor = FColor32( 0, 0, 0 );
	float32 ShadowOffsetX = 2.0f;
	float32 ShadowOffsetY = 2.0f;
};
