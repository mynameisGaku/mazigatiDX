// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUIRenderTargetView
//   描画先（レンダーターゲット）を、ウィジェットとして表示する。ミニマップ・3D のプレビュー・キャラクターの立ち絵・
//   ワールドの中のモニター・UI の特殊効果などに使う。
//
//   - 描画先を作る関数（SetTargetFactory）: 幅・高さ（画素）を受け取って、描画先の画像を返す。大きさが変わったときだけ呼ぶ
//     （例: Assets->CreateRenderTarget( "Minimap", Desc ) を、ラムダで包んで渡す。このクラスは、アセットも DxLib も知らない）。
//   - 描く関数（SetRenderFunction）: 描画先へ描く命令を、コマンドリストへ記録する（ここでは、描画先がすでに切り替わっている。
//     ライブの 3D ビューなら、カメラを替えて、ワールドを描く）。記録のあと、ブレンドと色は元（不透明・白）に戻る。
//   - 大きさ: ウィジェットの矩形の大きさ × 解像度の倍率（SetResolutionScale。画素に切り上げ、1〜MaxTargetSize に収める）。矩形が変われば、描画先を作り直す。
//   - 更新の頻度（SetUpdateInterval）: 1 = 描くたび / N = N 回に 1 回 / 0 = 要求したときだけ（Invalidate、または大きさが変わったとき）。
//     描かないフレームも、前に描いた内容を表示する。
//   - 入力の転送: ウィジェットの上のポインターの位置を、描画先の画素の座標に直す（ToViewPosition）。Interactive にすると、
//     押した・ドラッグ・離した位置が OnViewPressed / OnViewDragged / OnViewReleased で届く（中のオブジェクトを選ぶ・操作する）。
//     3D のビューなら、その位置から、カメラ（FCamera）で光線を作れる。
//   - 表示: 貼り方（既定は Stretch。Fit / Fill も可。入力の座標の変換も、同じ貼り方に合わせる）と、画像に掛ける色。
//=============================================================================
class CUIRenderTargetView : public CUIWidget
{
public:
	using FTargetFactory = TCallable<TSharedPtr<IImage>( int32 Width, int32 Height )>;
	using FRenderFunction = TCallable<void( FDrawContext&, int32 Width, int32 Height )>;

	void SetTargetFactory( const FTargetFactory& InFactory );
	void SetRenderFunction( const FRenderFunction& InFunction ) { RenderFunction = InFunction; }

	// 描画先の大きさの倍率（0 より大きい。既定は 1。0.5 なら、矩形の半分の解像度）
	void SetResolutionScale( float32 InScale );
	float32 GetResolutionScale() const { return ResolutionScale; }
	// 描画先の幅・高さの上限（画素。既定は 4096）
	void SetMaxTargetSize( int32 InMaxSize );
	// 更新の頻度（上の説明。負の値は 0 と同じ）
	void SetUpdateInterval( int32 InFrames );
	// 次に描くとき、描画先へ描き直させる
	void Invalidate() { bRenderRequested = true; }

	// 貼り方（Stretch / Fit / Fill。Tile と NineSlice は Stretch と同じ）と、画像に掛ける色
	void SetDisplayMode( EUIImageMode InMode ) { DisplayMode = InMode; }
	void SetTint( const FColor32& InTint ) { Tint = InTint; }

	// いまの描画先（まだ作っていなければ null）と、その大きさ（画素）
	const TSharedPtr<IImage>& GetTarget() const { return Target; }
	int32 GetTargetWidth() const { return TargetWidth; }
	int32 GetTargetHeight() const { return TargetHeight; }
	// 描画先へ描いた回数 / 描画先を作った回数（確認や、性能の目安に使える）
	int32 GetRenderCount() const { return RenderCount; }
	int32 GetCreateCount() const { return CreateCount; }

	// 画面の位置を、描画先の画素の座標に直す。描画先の中なら true（外でも、OutViewPosition には、直した値を入れる。ドラッグで外へ出た位置を使うため）。
	// 描画先がまだ無い・矩形が空なら、false
	bool ToViewPosition( const FVector2& ScreenPosition, FVector2& OutViewPosition ) const;

	// 押した・ドラッグ・離した位置（描画先の画素の座標）。Interactive のときだけ届く
	TUIEvent<CUIWidget&, const FVector2&> OnViewPressed;
	TUIEvent<CUIWidget&, const FVector2&> OnViewDragged;
	TUIEvent<CUIWidget&, const FVector2&> OnViewReleased;

protected:
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;
	void OnPointerPressed( const FVector2& Position ) override;
	void OnPointerDragged( const FVector2& Position ) override;
	void OnPointerReleased( const FVector2& Position ) override;

private:
	// 描画先の大きさを、矩形と倍率から決める（矩形が空なら、0 x 0）
	void ComputeTargetSize( int32& OutWidth, int32& OutHeight ) const;
	// 画面の位置から描画先の座標への、拡大率（画面の画素 / 描画先の画素）
	FVector2 GetViewToScreenScale() const;

	FTargetFactory TargetFactory;
	FRenderFunction RenderFunction;
	TSharedPtr<IImage> Target;
	int32 TargetWidth = 0;
	int32 TargetHeight = 0;
	float32 ResolutionScale = 1.0f;
	int32 MaxTargetSize = 4096;
	int32 UpdateInterval = 1;
	int32 PaintCounter = 0;
	bool bRenderRequested = true;
	EUIImageMode DisplayMode = EUIImageMode::Stretch;
	FColor32 Tint = FColor32( 255, 255, 255, 255 );
	int32 RenderCount = 0;
	int32 CreateCount = 0;
};
