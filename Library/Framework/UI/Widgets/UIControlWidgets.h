// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// 操作用の部品（ポインター・フォーカス・決定を受ける。入力は CUIContext が配る）
//   CUIButton   : 押す（OnClicked）。状態（通常・ホバー・押下・使えない・フォーカス）ごとのブラシ
//   CUICheckBox : 押すたびに、オン / オフを切り替える（OnToggled）
//   CUISlider   : つまみを動かして、値を決める（OnValueChanged）。ドラッグ・左右（または上下）の矢印で変える
//=============================================================================

// ボタンの見た目（状態ごとのブラシ）。何も描かない状態は、空のブラシ
struct FUIButtonStyle
{
	FUIBrush Normal;
	FUIBrush Hover;
	FUIBrush Pressed;
	FUIBrush Disabled;
	FUIBrush FocusOverlay;   // フォーカス中、状態のブラシの上に重ねる（枠など）
};

//-----------------------------------------------------------------------------
// CUIButton
//   Interactive・Focusable。状態のブラシは、使えない（祖先が使えない場合も）→ 押下 → ホバー → 通常 の順に選ぶ。
//   押したときに起こすイベントは、部品の OnClicked（決定・クリック）。中に好きな子（ラベル・画像）を入れられる。
//-----------------------------------------------------------------------------
class CUIButton : public CUIWidget
{
public:
	CUIButton();

	void SetStyle( const FUIButtonStyle& InStyle ) { Style = InStyle; }
	const FUIButtonStyle& GetStyle() const { return Style; }

	// いまの状態のブラシ（確認や、独自の描き方に使える）
	const FUIBrush& GetCurrentBrush() const;

protected:
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	FUIButtonStyle Style;
};

//-----------------------------------------------------------------------------
// CUICheckBox
//   押す（クリック・決定）たびに、オン / オフが切り替わり、OnToggled が起こる。見た目は、オフとオンのブラシ（と、重ねるブラシ）。
//-----------------------------------------------------------------------------
struct FUICheckBoxStyle
{
	FUIBrush Off;
	FUIBrush On;
	FUIBrush HoverOverlay;      // ホバー中に重ねる
	FUIBrush DisabledOverlay;   // 使えないとき重ねる
	FUIBrush FocusOverlay;      // フォーカス中に重ねる
};

class CUICheckBox : public CUIWidget
{
public:
	CUICheckBox();

	// オンか。変えると OnToggled が起こる（bNotify が false なら起こさない）
	void SetChecked( bool bInChecked, bool bNotify = true );
	bool IsChecked() const { return bChecked; }

	void SetStyle( const FUICheckBoxStyle& InStyle ) { Style = InStyle; }
	const FUICheckBoxStyle& GetStyle() const { return Style; }

	// 切り替わったとき（引数: この部品・新しい状態）
	TUIEvent<CUIWidget&, bool> OnToggled;

protected:
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	FUICheckBoxStyle Style;
	bool bChecked = false;
};

//-----------------------------------------------------------------------------
// CUISlider
//   値は Minimum〜Maximum（既定 0〜1）。Step が 0 より大きければ、その刻みに丸める。
//   ドラッグ: 押した位置・押している間の位置から値を決める（つまみの中心が、位置に来る。部品の外へ出ても、押している間は追う）。
//   矢印: フォーカス中の左右（縦向きなら下上）で、Step（0 なら全体の 5%）ずつ変える。値が端のときも、矢印は使った扱い（フォーカスは動かない）。
//   見た目: 溝（Track）→ 値までの中身（Fill。なくてよい）→ つまみ（Handle）の順に描く。
//-----------------------------------------------------------------------------
struct FUISliderStyle
{
	FUIBrush Track;
	FUIBrush Fill;
	FUIBrush Handle;
	FUIBrush HandleHover;   // ホバー中・押下中のつまみ（空なら Handle）
	FVector2 HandleSize = FVector2( 20.0f, 20.0f );
	FUIMargin TrackInset;   // 溝の描く範囲を、矩形から内側へ縮める量（溝を、矩形より細くするとき）
	FUIMargin FillInset;    // 中身（Fill）の描く範囲を、溝から内側へ縮める量（中身が、溝の枠・丸みの上に、はみ出さないように）
};

class CUISlider : public CUIWidget
{
public:
	CUISlider();

	void SetRange( float32 InMinimum, float32 InMaximum );
	float32 GetMinimum() const { return Minimum; }
	float32 GetMaximum() const { return Maximum; }
	void SetStep( float32 InStep ) { Step = NMath::Max( 0.0f, InStep ); }
	float32 GetStep() const { return Step; }

	// 値を決める（範囲に収め、Step に丸める）。変わったら OnValueChanged が起こる（bNotify が false なら起こさない）
	void SetValue( float32 InValue, bool bNotify = true );
	float32 GetValue() const { return Value; }
	// 0〜1 の位置（Minimum が 0、Maximum が 1）
	float32 GetNormalizedValue() const;

	void SetOrientation( EUIOrientation InOrientation ) { Orientation = InOrientation; }
	EUIOrientation GetOrientation() const { return Orientation; }
	void SetStyle( const FUISliderStyle& InStyle ) { Style = InStyle; }
	const FUISliderStyle& GetStyle() const { return Style; }

	// つまみの矩形・中身（溝の端から、つまみの中心まで。FillInset だけ内側）の矩形（確認や、独自の描き方に使える）
	FUIRect GetHandleRect() const;
	FUIRect GetFillRect() const;

	// 値が変わったとき（引数: この部品・新しい値）
	TUIEvent<CUIWidget&, float32> OnValueChanged;

protected:
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;
	void OnPointerPressed( const FVector2& Position ) override;
	void OnPointerDragged( const FVector2& Position ) override;
	bool OnNavigate( EUINavDirection Direction ) override;

private:
	void SetFromPosition( const FVector2& Position );

	FUISliderStyle Style;
	EUIOrientation Orientation = EUIOrientation::Horizontal;
	float32 Minimum = 0.0f;
	float32 Maximum = 1.0f;
	float32 Step = 0.0f;
	float32 Value = 0.0f;
};
