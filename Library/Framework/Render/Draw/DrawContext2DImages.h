// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContext2DShapes.h"

// 2Dの画像・スプライト・文字列
class FDrawContext2DImages : public FDrawContext2DShapes
{
public:
	explicit FDrawContext2DImages( FDrawBuffer& InBuffer ) : FDrawContext2DShapes( InBuffer ) {}

	//-------------------------------------------------------------------------
	// 2D の画像の描画（引数の順番は、DxLib の DrawGraph / DrawExtendGraph / DrawModiGraph / DrawRectGraph / DrawRotaGraph3 / DrawTile / DrawCircleGauge などに合わせてある）
	//   画像は、IImage を実装したもの（CTexture など。TSharedPtr・ポインター・参照のどれでもよい）か、FImageHandle で渡す。IImage が nullptr・無効（IsValid が false）なら、何も記録しない。
	//   記録のあと、実行される（メインスレッドで描かれる）まで、画像を手放さないこと。座標は float32 のピクセル（左上が原点）。画像は透過して描く（画像のアルファ・透過色が効く）。
	//   描けない値（無効な画像・座標が NaN・無限大・絶対値 1e7 超・切り出す矩形の大きさが 0 以下・拡大率が 0 以下など）は、何も記録しない。
	//   ブレンドモード（Blend）と描画の色（Tint）は、そのまま効く。
	//-------------------------------------------------------------------------
	// 画像を等倍で描く（DrawGraphF）。(X, Y) が画像の左上。反転すると、画像の中だけが反転する（左上の位置は同じ）
	void Image( float32 X, float32 Y, FImageHandle Handle, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void Image( float32 X, float32 Y, const TImage& Source, bool bFlipX = false, bool bFlipY = false )
	{
		Image( X, Y, ToHandle( Source ), bFlipX, bFlipY );
	}

	// 画像を、矩形 (X0, Y0)-(X1, Y1) に合わせて拡大・縮小して描く（DrawExtendGraphF）。X0 > X1 や Y0 > Y1 なら、その向きに反転する
	void ImageStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, FImageHandle Handle );

	template<typename TImage>
	FORCEINLINE void ImageStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, const TImage& Source )
	{
		ImageStretch( X0, Y0, X1, Y1, ToHandle( Source ) );
	}

	// 画像を、4 つの角へ自由に変形して描く（DrawModiGraphF）。画像の 左上・右上・右下・左下 が、(X0, Y0) (X1, Y1) (X2, Y2) (X3, Y3) へ行く
	void ImageQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, FImageHandle Handle );

	template<typename TImage>
	FORCEINLINE void ImageQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const TImage& Source )
	{
		ImageQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, ToHandle( Source ) );
	}

	// 画像の一部分（矩形 (SrcX, SrcY, SrcW, SrcH)）を、等倍で描く（DrawRectGraphF）。(X, Y) が、切り出した部分の左上。スプライトシートの 1 コマなどに
	void ImagePart( float32 X, float32 Y, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void ImagePart( float32 X, float32 Y, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source, bool bFlipX = false, bool bFlipY = false )
	{
		ImagePart( X, Y, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ), bFlipX, bFlipY );
	}

	// 画像の一部分を、矩形 (X0, Y0)-(X1, Y1) に合わせて拡大・縮小して描く（DrawRectExtendGraphF）。X0 > X1 や Y0 > Y1 なら、その向きに反転する
	void ImagePartStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle );

	template<typename TImage>
	FORCEINLINE void ImagePartStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source )
	{
		ImagePartStretch( X0, Y0, X1, Y1, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ) );
	}

	// 画像の一部分を、4 つの角へ自由に変形して描く（DrawRectModiGraphF）。切り出した部分の 左上・右上・右下・左下 が、(X0, Y0) (X1, Y1) (X2, Y2) (X3, Y3) へ行く
	void ImagePartQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle );

	template<typename TImage>
	FORCEINLINE void ImagePartQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source )
	{
		ImagePartQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ) );
	}

	// 画像全体を、基準点（画像の中の点 (PivotX, PivotY)。左上が原点、ピクセル）が画面の (X, Y) に来るように、横・縦に別々の拡大率で拡大し、基準点まわりに回して描く（DrawRotaGraph3F）。
	// 反転しても、基準点は (X, Y) のまま。AngleRadians はラジアン。拡大率が負なら、その向きに反転する
	void SpritePivot( float32 X, float32 Y, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void SpritePivot( float32 X, float32 Y, const TImage& Source, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		SpritePivot( X, Y, ToHandle( Source ), PivotX, PivotY, ScaleX, ScaleY, AngleRadians, bFlipX, bFlipY );
	}

	// 矩形 (X0, Y0)-(X1, Y1) を、画像の繰り返しで埋める（DrawTile）。座標は整数（DxLib の DrawTile と同じ）。
	// 画像の左上が、矩形の左上に合う（DxLib の DrawTile は画像の中心を矩形の中心に置くので、実行のときに補正する）。
	// OffsetX / OffsetY は並びの、右下向きのずれ（ピクセル。拡大率が 1 でないと、拡大率の整数倍に丸まる）、Scale は画像の拡大率（0 より大きい）、AngleRadians は画像の回転（ラジアン）
	void Tile( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Handle, int32 OffsetX = 0, int32 OffsetY = 0, float32 Scale = 1.0f, float32 AngleRadians = 0.0f );

	template<typename TImage>
	FORCEINLINE void Tile( int32 X0, int32 Y0, int32 X1, int32 Y1, const TImage& Source, int32 OffsetX = 0, int32 OffsetY = 0, float32 Scale = 1.0f, float32 AngleRadians = 0.0f )
	{
		Tile( X0, Y0, X1, Y1, ToHandle( Source ), OffsetX, OffsetY, Scale, AngleRadians );
	}

	// 円グラフのように、画像を、真上から時計回りに Fraction（0..1。範囲の外は収める）の割合まで描く（DrawCircleGaugeF）。クールダウンの表示などに。
	// (X, Y) が画像の中心。StartFraction（0..1）は描き始める位置（真上が 0）で、そこから Fraction だけ描く（描き終わりが 1 を超えると、先頭へ回り込む。Fraction が 1 なら、描き始めの位置に関係なく全体）。
	// Scale は画像の拡大率（0 より大きい）。bFlipX / bFlipY は、グラフを左右・上下に反転する。DxLib のゲージは、画像の四角ではなく、画像の中心を中心とする円（半径は、画像の半分より少し大きい）を描く
	void CircleGauge( float32 X, float32 Y, float32 Fraction, FImageHandle Handle, float32 StartFraction = 0.0f, float32 Scale = 1.0f, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void CircleGauge( float32 X, float32 Y, float32 Fraction, const TImage& Source, float32 StartFraction = 0.0f, float32 Scale = 1.0f, bool bFlipX = false, bool bFlipY = false )
	{
		CircleGauge( X, Y, Fraction, ToHandle( Source ), StartFraction, Scale, bFlipX, bFlipY );
	}

	// 以降の描画に掛ける色（SetDrawBright）。白に戻すと元の色。そのオブジェクトの分が終わると白に戻る
	void Tint( const FColor32& Color );

	// 以降のブレンドモードを変える（SetDrawBlendMode）。Param は Alpha のときの不透明度（0..255。範囲の外は収める）など。
	// Custom と、EDrawBlend にない値は、何も記録しない（カスタムブレンドは CustomBlend で）。そのオブジェクトの分が終わると、ブレンドしない状態に戻る
	void Blend( EDrawBlend Mode, int32 Param = 255 );

	// カスタムブレンド（SetDrawCustomBlendMode）。Src（これから描く色）と Dest（描き先の色）に係数を掛けて、組み合わせ方で合わせる。
	// 例: 加算 = { SrcColor: SrcAlpha, DestColor: One, ColorOp: Add }。Blend で別のモードにするか、そのオブジェクトの分が終わると、元に戻る。
	// 係数・組み合わせ方が範囲の外のものは、何も記録しない
	void CustomBlend( const FCustomBlend& Settings );

	// このDrawContextで最後に記録した合成方法（カスタムブレンドのあとは Custom）。初期状態はOpaque。
	EDrawBlend GetBlendMode() const;
	// このDrawContextで最後に記録した合成倍率。初期状態は255。
	int32 GetBlendParam() const;

	// 最後に記録した合成方法を、まとめて取り出す（あとで SetBlendState で戻すため）
	FBlendState GetBlendState() const;

	// GetBlendState で取り出した合成方法に戻す。Custom なら CustomBlend、それ以外は Blend を記録する
	void SetBlendState( const FBlendState& State );

	// 以降の描画の色に足す色（SetDrawAddColor。Tint で掛けたあとに足される）。各成分は -255..255（負なら引く。範囲の外は収める）。(0, 0, 0) が元の状態。
	// そのオブジェクトの分が終わると、(0, 0, 0) に戻る
	void AddColor( int32 R, int32 G, int32 B );

	// アルファテスト（SetDrawAlphaTest）。描く色のアルファ値と Reference（0..255。範囲の外は収める）を Compare で比べて、成り立つ画素だけ描く。
	// 切り抜きの画像（葉・柵など）の縁を、半透明にせずに抜くときなどに。AlphaTestOff か、そのオブジェクトの分が終わると、元（しない）に戻る。Compare が範囲の外なら、何も記録しない
	void AlphaTest( EDrawCompare Compare, int32 Reference );

	// アルファテストをやめる
	void AlphaTestOff();

	// 画像を拡大・縮小・回転して描くときの色の決め方（SetDrawMode）。Nearest はギザギザ（ドット絵向き）、Bilinear はなめらか。
	// そのオブジェクトの分が終わると、始める前の状態に戻る。範囲の外の値は、何も記録しない
	void Filter( EDrawFilter Mode );

	// クリップ範囲を、いまの範囲と (X0, Y0)-(X1, Y1)（右と下の端は含まない）の共通部分にする（SetDrawArea）。入れ子にできて、PopClip で 1 つ前の範囲に戻る
	// （同じ数だけ呼ぶ。スコープで対にするなら FClipScope）。共通部分が空なら、何も描かれない。そのオブジェクトの分が終わると、始める前の範囲に戻る。
	// 値が絶対値 1e7 超のときは、何も記録せず false を返す（対の PopClip も呼ばないこと）
	bool PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 );

	// 1 つ前のクリップ範囲に戻す。PushClip の数より多く呼んでも、何も起きない
	void PopClip();

	// 以降の 2D の描画（図形・画像・文字）に掛ける変換（SetTransformTo2D）。画面の座標（ピクセル）に掛かる。アフィン変換だけ（最後の列は無視される）。
	// 2D のカメラ（スクロール・拡大・回転）に使える。ResetTransform2D か、そのオブジェクトの分が終わると、元（変換しない）に戻る。
	// 値が NaN・無限大・絶対値 1e7 超のときは、何も記録しない
	void Transform2D( const FMatrix4x4& Matrix );

	// 2D の変換を、元（変換しない）に戻す
	void ResetTransform2D();
};
