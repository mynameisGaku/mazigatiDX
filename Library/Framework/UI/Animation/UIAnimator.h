// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/UI/Animation/UIEasing.h"
#include "Library/Framework/UI/Core/UIWidget.h"

// 動かせる、部品の値
enum class EUIAnimatedProperty : uint8
{
	Opacity,        // 不透明度（0〜1）
	TranslationX,   // 描くときのずれ（画素。SetRenderTransform）
	TranslationY,
	Scale,          // 描くときの倍率（縦横とも）
	ScaleX,
	ScaleY,
	Rotation        // 描くときの回転（ラジアン）
};

// 終わったあとの動き
enum class EUIAnimationLoop : uint8
{
	Once,       // 終わりの値で止まる
	Loop,       // 始めの値に戻って、繰り返す
	PingPong    // 行って、戻って、を繰り返す
};

struct FUIAnimationOptions
{
	float32 Duration = 0.25f;                       // 秒（0 以下なら、すぐ終わりの値になる）
	float32 Delay = 0.0f;                           // 始めるまでの待ち時間（秒。待つ間は、始めの値）
	EUIEasing Easing = EUIEasing::CubicOut;
	EUIAnimationLoop Loop = EUIAnimationLoop::Once;
	TCallable<void()> OnFinished;                   // Once で、終わりの値になったとき（Stop で止めたときは、呼ばない）
};

//=============================================================================
// CUIAnimator
//   部品の値（不透明度・描くときのずれ・倍率・回転）や、好きな値を、時間をかけて動かす（補間）。レイアウトは変えないので、動かしても、ほかの部品は動かない。
//   ホストが 1 つ持ち、毎フレーム Update( 秒 ) を呼ぶ。メインスレッドだけで使う。
//
//   CUIAnimator Animator;
//   Animator.Play( Panel, EUIAnimatedProperty::Opacity, 0.0f, 1.0f );                          // 0.25 秒で、ふわっと出す
//   FUIAnimationOptions Slide;
//   Slide.Duration = 0.4f;
//   Slide.Easing = EUIEasing::BackOut;
//   Animator.Play( Panel, EUIAnimatedProperty::TranslationY, 40.0f, 0.0f, Slide );             // 下から、少し行き過ぎて止まる
//   Animator.PlayValue( 0.0f, 100.0f, Options, [&]( float32 Value ) { Bar->SetValue( Value ); } );   // 好きな値
//   Animator.Update( DeltaTime );                                                             // 毎フレーム
//
//   - 同じ部品の同じ値を、もう一度 Play すると、前の動きを置き換える（2 つの動きが、取り合わない）。
//   - 部品は、弱い参照で持つ。部品が消えたら、その動きも消える（OnFinished は、呼ばない）。
//   - Play は、その場で、始めの値を入れる（待ち時間の間に、終わりの値が一瞬見えることがない）。
//   - Update の最中（OnFinished や、値を入れる関数の中）に、Play・Stop を呼んでもよい。
//=============================================================================
class CUIAnimator
{
public:
	// 部品の値を、From から To へ動かす。戻り値は、動きの番号（1 以上。Stop・IsPlaying に使う）。部品が null なら 0
	int32 Play( const TSharedPtr<CUIWidget>& Widget, EUIAnimatedProperty Property, float32 From, float32 To, const FUIAnimationOptions& Options = FUIAnimationOptions() );
	// いまの値から、To へ動かす
	int32 PlayTo( const TSharedPtr<CUIWidget>& Widget, EUIAnimatedProperty Property, float32 To, const FUIAnimationOptions& Options = FUIAnimationOptions() );
	// 好きな値を動かす（Apply に、いまの値を渡す）。Apply が空なら 0
	int32 PlayValue( float32 From, float32 To, const FUIAnimationOptions& Options, const TCallable<void( float32 )>& Apply );

	// 動きを止める（値は、いまのまま）。なければ false
	bool Stop( int32 Id );
	// 部品の動きを、すべて止める。止めた数を返す
	int32 StopAll( const CUIWidget& Widget );
	void Clear();
	// 動きを、終わりの値まで進めて、終える（Once だけ。OnFinished を呼ぶ）。繰り返す動きは、止めるだけ。なければ false
	bool Finish( int32 Id );

	bool IsPlaying( int32 Id ) const;
	// 部品に、動いている値があるか
	bool IsAnimating( const CUIWidget& Widget ) const;
	int32 Num() const { return Tweens.Num(); }

	// 時間を進める（秒。負は 0 として扱う）
	void Update( float32 DeltaTime );

	// 部品の、いまの値を読む・入れる
	static float32 GetProperty( const CUIWidget& Widget, EUIAnimatedProperty Property );
	static void SetProperty( CUIWidget& Widget, EUIAnimatedProperty Property, float32 Value );

private:
	struct FTween
	{
		int32 Id = 0;
		TWeakPtr<CUIWidget> Widget;
		bool bCustom = false;
		EUIAnimatedProperty Property = EUIAnimatedProperty::Opacity;
		TCallable<void( float32 )> Apply;
		float32 From = 0.0f;
		float32 To = 0.0f;
		float32 Elapsed = 0.0f;      // 待ち時間を含む
		FUIAnimationOptions Options;
	};

	int32 AddTween( FTween& Tween );
	// いまの時間での値を入れる。部品が消えていたら false
	static bool ApplyTween( const FTween& Tween, float32 Amount );
	int32 FindIndex( int32 Id ) const;

	TArray<FTween> Tweens;
	int32 NextId = 1;
};
