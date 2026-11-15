// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Math/Color.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/RenderTextFont.h"

// APIに依存しない2D実行先。フロント側はゲームスレッド・フレーム内で使用する。
// 記録した値を別の実行先へ渡す場合、その実行先は定めた所有スレッドだけで操作する。
// 呼び出し側は有限座標、正の寸法と太さ、凸な点列を保証する。
class IRenderCanvas2D
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderCanvas2D() = default;
	// 矩形を描く。座標は論理画素。
	virtual void Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness ) = 0;
	// 線分を描く。
	virtual void Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness ) = 0;
	// 円を描く。Segmentsの0は自動分割。
	virtual void Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments ) = 0;
	// 三角形を描く。座標はXYの順で3点。
	virtual void Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness ) = 0;
	// 凸四角形を描く。座標はXYの順で4点。
	virtual void Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness ) = 0;
	// 凸な点列（XYの順でCount点。3〜512点）を描く。未対応の実行先は何も描かない。
	virtual void Polygon( const float32* /*Points*/, int32 /*Count*/, FColor32 /*Color*/, bool /*bFill*/, float32 /*Thickness*/ ) {}
	// 既定フォントでUTF-8の範囲を描く。
	virtual void Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End ) = 0;
	// フォントを指定して、UTF-8の範囲を描く。Transformは、文字の画像の中の点 (x, y)（画素。左上が原点）を、画面の点
	// ( x * T[0] + y * T[2] + T[4], x * T[1] + y * T[3] + T[5] ) へ移す（位置・拡大・回転）。縁取りのあるフォントでは、縁取りをEdgeColorで先に描く。
	// 未対応の実行先は何も描かない。
	virtual void TextFont( const FRenderTextFont& /*Font*/, const float32 ( & /*Transform*/ )[6], FColor32 /*Color*/, FColor32 /*EdgeColor*/, const char* /*Begin*/, const char* /*End*/ ) {}
	// 借用画像を凸四角形に貼る。UVは左上と右下。
	virtual void Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color ) = 0;
	// 現在の範囲との共通部分を積む。空なら描かない。
	virtual void PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 ) = 0;
	// 直前のクリップ範囲へ戻す。Pushと対にする。
	virtual void PopClip() = 0;
	// 次の画像の補間を指定する。未対応ならfalse。
	virtual bool SetTextureFilter( bool /*bLinear*/ ) { return false; }
	// 図形の端の補間を指定する。
	virtual void SetAntiAlias( bool bEnabled ) = 0;
	// 以降の図形・画像・文字の、描画先との混ぜ方を指定する。未対応の実行先は通常の合成のまま。
	virtual void SetBlend( EGeometryBlend /*Blend*/ ) {}
};
