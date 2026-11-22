// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContextCore.h"

// いまの合成方法をまとめたもの。ほかの合成方法で少し描いてから元に戻すときに、FDrawContext::GetBlendState で保存し、SetBlendState で戻す
// （Blend だけでは、CustomBlend で決めた合成方法を戻せない）
struct FBlendState
{
	// 合成の種類（カスタムブレンドのあとは Custom）
	EDrawBlend Mode = EDrawBlend::Opaque;
	// Alpha などの強さ（Mode が Custom のときは、Custom.Param と同じ）
	int32 Param = 255;
	// Mode が Custom のときの設定
	FCustomBlend Custom;
};

// 2Dの平らな図形（四角・線・円・三角形・多角形）と、合成方法
class FDrawContext2DShapes : public FDrawContextCore
{
public:
	explicit FDrawContext2DShapes( FDrawBuffer& InBuffer ) : FDrawContextCore( InBuffer ) {}

	//-------------------------------------------------------------------------
	// 2D（画面座標）
	//-------------------------------------------------------------------------
	// 四角形（DrawBox）。bFill が false なら枠線だけ（Thickness は枠の太さ）
	void Box( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, bool bFill = true, int32 Thickness = 1 );

	// 円（DrawCircle）
	void Circle( int32 X, int32 Y, int32 Radius, const FColor32& Color, bool bFill = true, int32 Thickness = 1 );

	// 線（DrawLine）
	void Line( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, int32 Thickness = 1 );

	// 点（DrawPixel）
	void Pixel( int32 X, int32 Y, const FColor32& Color );

	// 三角形（DrawTriangle）。頂点は (X0, Y0) (X1, Y1) (X2, Y2)。bFill が false なら枠線だけ（太さは 1）
	void Triangle( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, const FColor32& Color, bool bFill = true );

	// 四角形（DrawQuadrangle）。頂点を (X0, Y0) -> (X1, Y1) -> (X2, Y2) -> (X3, Y3) の順にたどる（台形や平行四辺形など、傾いた四角形用。軸に平行な四角形は Box）
	void Quad( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 X3, int32 Y3, const FColor32& Color, bool bFill = true );

	// 楕円（DrawOval）。(X, Y) が中心、Rx / Ry が横と縦の半径
	void Oval( int32 X, int32 Y, int32 Rx, int32 Ry, const FColor32& Color, bool bFill = true, int32 Thickness = 1 );

	// 角の丸い四角形（DrawRoundRect）。Rx / Ry が角の丸みの横と縦の半径。bFill が false なら枠線だけ（太さは 1）
	void RoundBox( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Rx, int32 Ry, const FColor32& Color, bool bFill = true );

	//-------------------------------------------------------------------------
	// なめらかな（アンチエイリアス付きの）2D 図形。座標は float32 で、ピクセルの途中にも置ける（ゆっくり動く物がガタつかない）。
	// 数値が有限でない（NaN・無限大）・大きすぎる（絶対値が 1e7 より大きい）・半径が負・太さが 0 以下のものは、記録しない。
	// NumSegments は円・楕円・角の丸みを何角形で近似するか。0 なら、半径から（誤差が 1/4 ピクセル以下になるように）決める
	//-------------------------------------------------------------------------
	// 線（DrawLineAA）
	void LineAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, float32 Thickness = 1.0f );

	// 四角形（DrawBoxAA）。bFill が false なら枠線だけ
	void BoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f );

	// 円（DrawCircleAA）
	void CircleAA( float32 X, float32 Y, float32 Radius, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 );

	// 楕円（DrawOvalAA）。(X, Y) が中心、Rx / Ry が横と縦の半径
	void OvalAA( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 );

	// 三角形（DrawTriangleAA）
	void TriangleAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f );

	// 四角形（DrawQuadrangleAA）。頂点を順にたどる
	void QuadAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f );

	// 角の丸い四角形（DrawRoundRectAA）。Rx / Ry が角の丸みの横と縦の半径
	void RoundBoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 );

	// 文字列（DrawString）。文字列は記録のときにコピーされる（呼び出したあと、元の領域は使い回してよい）
	void Text( int32 X, int32 Y, const char* String, const FColor32& Color );
	// 書式付きの文字列（DrawFormatString）
	void TextF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... );

	// ワールド座標の帯を記録し、点列を複写する。補助点の区間は描かず、2..1024点以外や不正な数値は記録しない。
	// Widthは全幅、Uは累積距離。Texture=-1は色だけの帯。既存のWorldMatrixには影響されない。
	// 画像を整数U境界で分割した区間が8192を超える指定も、記録しない。
	void Ribbon3D( const FDrawRibbonData::FPoint* Points, int32 NumPoints, int32 Texture, float32 UVRepeat = 1.0f, int32 JoinFlags = 0 );

	// フォント指定の文字列（DrawStringToHandle）。FontHandle は CFont::GetHandle()。
	// Align で、X を文字列の左端 / 中央 / 右端にできる（幅の計算は実行のとき、メインスレッドで行う）。EdgeColor は縁取りのあるフォントのときの縁の色
	void TextWithFont( int32 FontHandle, int32 X, int32 Y, const char* String, const FColor32& Color, EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black );

	// 拡大・回転・縦書き・影のある文字列（DrawRotaStringFToHandle）。FontHandle は CFont::GetHandle()（負なら、既定のフォント）。
	// (X, Y) は、文字列の中の基準点（Style.AnchorX / AnchorY）が来る画面の位置（小数でもよい）。文字列は、記録のときにコピーされる。
	// 位置・拡大率・回転・基準点・影のずれが有限でない・大きすぎる（絶対値 1e7 超）、拡大率が 0 以下、文字列が nullptr なら、何も記録せず false を返す。
	// 影をつけると、影の文字列が先に、続けて本体が記録される（2 つの命令になる）
	bool TextStyled( int32 FontHandle, float32 X, float32 Y, const char* String, const FColor32& Color, const FTextStyle& Style = FTextStyle() );

	// 画像（DrawRotaGraphF）。(X, Y) が画像の中心。AngleRadians はラジアン。GraphHandle は LoadGraph などで得たもの
	void Sprite( float32 X, float32 Y, int32 GraphHandle, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	// 画像の一部分（スプライトシートの 1 コマなど）を描く（DrawRectRotaGraph3F）。
	// 画像 GraphHandle の矩形 (SrcX, SrcY, SrcW, SrcH) のうち、矩形の中の点 (PivotX, PivotY)（矩形の左上が原点、ピクセル）が画面の (X, Y) に来る。
	// AngleRadians は Pivot まわりの回転、反転も Pivot を軸にする（反転しても Pivot は (X, Y) のまま）
	void SpriteRect( float32 X, float32 Y, int32 GraphHandle, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	// 画像（IImage を実装したもの。CTexture など）を渡す版の Sprite / SpriteRect。画像が nullptr・無効なら、何も記録しない
	template<typename TImage>
	FORCEINLINE void Sprite( float32 X, float32 Y, const TImage& Source, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		const FImageHandle Handle = ToHandle( Source );
		if ( Handle.IsValid() )
		{
			Sprite( X, Y, Handle.Value, Scale, AngleRadians, bFlipX, bFlipY );
		}
	}

	template<typename TImage>
	FORCEINLINE void SpriteRect( float32 X, float32 Y, const TImage& Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 PivotX, float32 PivotY,
								 float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		const FImageHandle Handle = ToHandle( Source );
		if ( Handle.IsValid() )
		{
			SpriteRect( X, Y, Handle.Value, SrcX, SrcY, SrcW, SrcH, PivotX, PivotY, ScaleX, ScaleY, AngleRadians, bFlipX, bFlipY );
		}
	}

protected:
	// Blend呼び出しで更新する、この記録窓口内の現在合成方法。
	EDrawBlend CurrentBlendMode = EDrawBlend::Opaque;
	// 現在合成方法に渡した倍率。
	int32 CurrentBlendParam = 255;
	// 最後に記録したカスタムブレンドの設定（現在の合成方法が Custom のときだけ意味がある）。
	FCustomBlend CurrentCustomBlend;};
