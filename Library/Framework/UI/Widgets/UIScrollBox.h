// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUIScrollBox
//   中身（子）が、枠より大きいとき、スクロールして見せる。子は、矩形でクリップされる（外へはみ出した部分は、描かれず、当たらない）。
//   向きは縦（既定）または横。主軸の向きは、子を、ほしい大きさ（枠より小さければ、枠の大きさ）で置き、スクロールの量だけずらす。
//   交差軸は、枠の幅（高さ）いっぱい（スクロールバーが出ているときは、バーの分だけ狭くなる）。
//
//   - ホイール: ポインターが枠の上にあるとき、回すとスクロールする（SetWheelStep の画素ずつ）。スクロールできない（中身が収まっている）ときは、
//     ホイールを使わない扱いにして、外側のスクロール枠へ伝える。
//   - フォーカス: 中の部品がフォーカスを得ると、その部品が見える位置まで、自動でスクロールする（矢印・Tab で動いても、見失わない）。
//   - スクロールバー: 中身が収まらないときだけ、枠の端に出る（溝とつまみのブラシ。つまみの長さは、見えている割合）。いまは、見せるだけ（つまみのドラッグはしない）。
//   - ほしい大きさ: 主軸は 0（親の置き方か、FixedWidth / FixedHeight で決める）、交差軸は、子のほしい大きさの最大。
//=============================================================================
class CUIScrollBox : public CUIWidget
{
public:
	CUIScrollBox();

	void SetOrientation( EUIOrientation InOrientation );
	EUIOrientation GetOrientation() const { return Orientation; }

	// スクロールの量（画素。0〜GetMaxScrollOffset に収める）
	void SetScrollOffset( float32 InOffset );
	float32 GetScrollOffset() const { return ScrollOffset; }
	// 最大のスクロール量（中身の大きさ − 枠の大きさ。収まっていれば 0。配置のあとで有効）
	float32 GetMaxScrollOffset() const { return NMath::Max( 0.0f, ContentExtent - ViewportExtent ); }
	// 中身の主軸の大きさと、枠（見える範囲）の主軸の大きさ（配置のあとで有効）
	float32 GetContentExtent() const { return ContentExtent; }
	float32 GetViewportExtent() const { return ViewportExtent; }

	// ホイール 1 段あたりの画素（既定は 48）
	void SetWheelStep( float32 InStep ) { WheelStep = NMath::Max( 1.0f, InStep ); }

	// 画面の矩形 Target が見える位置まで、スクロールする（すでに見えていれば、何もしない）
	void ScrollIntoView( const FUIRect& Target );

	// スクロールバー（太さが 0 なら出さない）
	void SetScrollBar( const FUIBrush& InTrack, const FUIBrush& InThumb, float32 InWidth );
	// つまみの矩形（見せているとき。出ていなければ、大きさ 0）
	FUIRect GetThumbRect() const;
	FUIRect GetTrackRect() const;

protected:
	FVector2 ComputeContentSize() const override;
	void ArrangeChildren( const FUIRect& ContentRect ) override;
	bool OnWheel( const FVector2& Position, float32 Delta ) override;
	void OnChildFocused( CUIWidget& Child ) override;
	void OnPaintOverlay( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	bool IsVertical() const { return Orientation == EUIOrientation::Vertical; }
	bool IsScrollBarShown() const { return ScrollBarWidth > 0.0f && GetMaxScrollOffset() > 0.0f; }

	EUIOrientation Orientation = EUIOrientation::Vertical;
	float32 ScrollOffset = 0.0f;
	float32 ContentExtent = 0.0f;
	float32 ViewportExtent = 0.0f;
	float32 WheelStep = 48.0f;
	FUIBrush TrackBrush;
	FUIBrush ThumbBrush;
	float32 ScrollBarWidth = 0.0f;
};
