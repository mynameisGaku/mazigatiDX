// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// 表示用の部品（入力を受けない）
//   CUIImage       : ブラシ（画像・色・9 分割・好きな描き方）を表示する
//   CUILabel       : 文字を表示する（フォント・色・寄せ方・折り返し）
//   CUIProgressBar : 0〜1 の割合を、背景と、割合の分だけ描く中身で表示する
//=============================================================================

//-----------------------------------------------------------------------------
// CUIImage
//   中身のブラシを、内側の矩形（Padding を引いたもの）いっぱいに描く。ほしい大きさは、中身が画像なら、その画像の大きさ
//   （それ以外は 0。FixedWidth / FixedHeight で決める）。
//-----------------------------------------------------------------------------
class CUIImage : public CUIWidget
{
public:
	// 画像を表示する（ImageSize は画像の大きさ。画素）。貼り方は SetMode / SetNineSliceMargin、色は SetTint
	void SetImage( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, EUIImageMode InMode = EUIImageMode::Stretch );
	void SetMode( EUIImageMode InMode );
	void SetNineSliceMargin( const FUIMargin& InMargin );
	void SetTint( const FColor32& InTint );

	// どんなブラシでも表示できる
	void SetContent( const FUIBrush& InContent );
	const FUIBrush& GetContent() const { return Content; }

protected:
	FVector2 ComputeContentSize() const override;
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	FUIBrush Content;
};

//-----------------------------------------------------------------------------
// CUILabel
//   文字を、内側の矩形（Padding を引いたもの）の中に、寄せ方（HorizontalTextAlignment / VerticalTextAlignment）に従って描く。
//   ほしい大きさは、文字の大きさ。SetMaxWidth（0 より大きい値）を指定すると、その幅で折り返す（ほしい大きさにも、折り返した結果が入る）。
//   フォントが無い・文字列が空なら、何も描かず、ほしい大きさは 0。
//   文字を測る・並べるのは、CFont（メインスレッドで呼ぶ）。色のアルファと不透明度は、描くときのブレンドに掛かる。
//-----------------------------------------------------------------------------
class CUILabel : public CUIWidget
{
public:
	void SetText( const FString& InText );
	const FString& GetText() const { return Text; }
	void SetFont( const TSharedPtr<CFont>& InFont );
	void SetColor( const FColor32& InColor ) { Color = InColor; }
	const FColor32& GetColor() const { return Color; }
	void SetEdgeColor( const FColor32& InEdgeColor ) { EdgeColor = InEdgeColor; }
	// 折り返す幅（画素）。0 以下なら折り返さない（改行だけで分ける）
	void SetMaxWidth( float32 InMaxWidth );
	// 行数の上限（0 は無制限）
	void SetMaxLines( int32 InMaxLines );
	// 内側の矩形の中での、文字の寄せ方（既定は左・上）。Stretch は Start と同じ
	void SetHorizontalTextAlignment( EUIAlignment InAlignment ) { HorizontalTextAlignment = InAlignment; }
	void SetVerticalTextAlignment( EUIAlignment InAlignment ) { VerticalTextAlignment = InAlignment; }

protected:
	FVector2 ComputeContentSize() const override;
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	FTextLayout MakeLayout( float32 WrapWidth ) const;
	float32 GetBlockHeight( const FTextLayout& Layout ) const;

	FString Text;
	TSharedPtr<CFont> Font;
	FColor32 Color = FColor32( 255, 255, 255, 255 );
	FColor32 EdgeColor = FColor32( 0, 0, 0, 255 );
	float32 MaxWidth = 0.0f;
	int32 MaxLines = 0;
	EUIAlignment HorizontalTextAlignment = EUIAlignment::Start;
	EUIAlignment VerticalTextAlignment = EUIAlignment::Start;
};

//-----------------------------------------------------------------------------
// CUIProgressBar
//   背景のブラシを矩形いっぱいに描き、その上に、中身のブラシを、割合（0〜1）の分だけ描く（横向きは左から、縦向きは下から）。
//   中身は、背景から FillInset だけ内側の領域の中に描く。ほしい大きさは 0（FixedWidth / FixedHeight で決める）。
//-----------------------------------------------------------------------------
class CUIProgressBar : public CUIWidget
{
public:
	// 割合（0〜1。範囲の外は収める）
	void SetValue( float32 InValue ) { Value = NMath::Clamp( InValue, 0.0f, 1.0f ); }
	float32 GetValue() const { return Value; }
	void SetOrientation( EUIOrientation InOrientation ) { Orientation = InOrientation; }
	void SetBackBrush( const FUIBrush& InBrush ) { BackBrush = InBrush; }
	void SetFillBrush( const FUIBrush& InBrush ) { FillBrush = InBrush; }
	void SetFillInset( const FUIMargin& InInset ) { FillInset = InInset; }

	// 中身を描く矩形（割合を反映したもの。割合が 0 なら大きさ 0）。確認や、独自の描き方に使える
	FUIRect GetFillRect() const;

protected:
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	float32 Value = 0.0f;
	EUIOrientation Orientation = EUIOrientation::Horizontal;
	FUIBrush BackBrush;
	FUIBrush FillBrush;
	FUIMargin FillInset;
};
