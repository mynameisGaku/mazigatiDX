// SPDX-License-Identifier: MIT
#include "UIAnimator.h"

#include "Library/Math/MathUtils.h"

float32 CUIAnimator::GetProperty( const CUIWidget& Widget, EUIAnimatedProperty Property )
{
	const FUIRenderTransform& Transform = Widget.GetRenderTransform();
	switch ( Property )
	{
	case EUIAnimatedProperty::TranslationX:
		return Transform.Translation.x;
	case EUIAnimatedProperty::TranslationY:
		return Transform.Translation.y;
	case EUIAnimatedProperty::Scale:
	case EUIAnimatedProperty::ScaleX:
		return Transform.Scale.x;
	case EUIAnimatedProperty::ScaleY:
		return Transform.Scale.y;
	case EUIAnimatedProperty::Rotation:
		return Transform.Rotation;
	default:
		return Widget.GetOpacity();
	}
}

void CUIAnimator::SetProperty( CUIWidget& Widget, EUIAnimatedProperty Property, float32 Value )
{
	if ( Property == EUIAnimatedProperty::Opacity )
	{
		Widget.SetOpacity( Value );
		return;
	}
	FUIRenderTransform Transform = Widget.GetRenderTransform();
	switch ( Property )
	{
	case EUIAnimatedProperty::TranslationX:
		Transform.Translation.x = Value;
		break;
	case EUIAnimatedProperty::TranslationY:
		Transform.Translation.y = Value;
		break;
	case EUIAnimatedProperty::Scale:
		Transform.Scale = FVector2( Value, Value );
		break;
	case EUIAnimatedProperty::ScaleX:
		Transform.Scale.x = Value;
		break;
	case EUIAnimatedProperty::ScaleY:
		Transform.Scale.y = Value;
		break;
	default:
		Transform.Rotation = Value;
		break;
	}
	Widget.SetRenderTransform( Transform );
}

bool CUIAnimator::ApplyTween( const FTween& Tween, float32 Amount )
{
	const float32 Value = Tween.From + ( Tween.To - Tween.From ) * Amount;
	if ( Tween.bCustom )
	{
		Tween.Apply( Value );
		return true;
	}
	const TSharedPtr<CUIWidget> Widget = Tween.Widget.Pin();
	if ( !Widget )
	{
		return false;
	}
	SetProperty( *Widget, Tween.Property, Value );
	return true;
}

int32 CUIAnimator::AddTween( FTween& Tween )
{
	Tween.Id = NextId++;
	const int32 Id = Tween.Id;
	// 入れる前に、写しを取る（値を入れる関数の中で Play されて、配列が動いても、安全なように）
	const FTween Copy = Tween;
	Tweens.Add( Copy );
	ApplyTween( Copy, 0.0f );
	return Id;
}

int32 CUIAnimator::Play( const TSharedPtr<CUIWidget>& Widget, EUIAnimatedProperty Property, float32 From, float32 To, const FUIAnimationOptions& Options )
{
	if ( !Widget )
	{
		return 0;
	}
	// 同じ部品の同じ値の動きを、置き換える（Scale は、ScaleX・ScaleY とも取り合う）
	for ( int32 Index = Tweens.Num() - 1; Index >= 0; --Index )
	{
		const FTween& Other = Tweens[Index];
		if ( Other.bCustom || Other.Widget.Pin().Get() != Widget.Get() )
		{
			continue;
		}
		const bool bScaleClash = ( Property == EUIAnimatedProperty::Scale && ( Other.Property == EUIAnimatedProperty::ScaleX || Other.Property == EUIAnimatedProperty::ScaleY ) )
								 || ( Other.Property == EUIAnimatedProperty::Scale && ( Property == EUIAnimatedProperty::ScaleX || Property == EUIAnimatedProperty::ScaleY ) );
		if ( Other.Property == Property || bScaleClash )
		{
			Tweens.RemoveAt( Index );
		}
	}
	FTween Tween;
	Tween.Widget = Widget;
	Tween.Property = Property;
	Tween.From = From;
	Tween.To = To;
	Tween.Options = Options;
	return AddTween( Tween );
}

int32 CUIAnimator::PlayTo( const TSharedPtr<CUIWidget>& Widget, EUIAnimatedProperty Property, float32 To, const FUIAnimationOptions& Options )
{
	if ( !Widget )
	{
		return 0;
	}
	return Play( Widget, Property, GetProperty( *Widget, Property ), To, Options );
}

int32 CUIAnimator::PlayValue( float32 From, float32 To, const FUIAnimationOptions& Options, const TCallable<void( float32 )>& Apply )
{
	if ( !Apply )
	{
		return 0;
	}
	FTween Tween;
	Tween.bCustom = true;
	Tween.Apply = Apply;
	Tween.From = From;
	Tween.To = To;
	Tween.Options = Options;
	return AddTween( Tween );
}

int32 CUIAnimator::FindIndex( int32 Id ) const
{
	for ( int32 Index = 0; Index < Tweens.Num(); ++Index )
	{
		if ( Tweens[Index].Id == Id )
		{
			return Index;
		}
	}
	return -1;
}

bool CUIAnimator::Stop( int32 Id )
{
	const int32 Index = FindIndex( Id );
	if ( Index < 0 )
	{
		return false;
	}
	Tweens.RemoveAt( Index );
	return true;
}

int32 CUIAnimator::StopAll( const CUIWidget& Widget )
{
	int32 Count = 0;
	for ( int32 Index = Tweens.Num() - 1; Index >= 0; --Index )
	{
		if ( !Tweens[Index].bCustom && Tweens[Index].Widget.Pin().Get() == &Widget )
		{
			Tweens.RemoveAt( Index );
			++Count;
		}
	}
	return Count;
}

void CUIAnimator::Clear()
{
	Tweens.Empty();
	StateMotions.Empty();
}

bool FUIStateMotion::IsIdentity() const
{
	const bool bNoScale = HoverScale == 1.0f && PressedScale == 1.0f && FocusScale == 1.0f;
	const bool bNoOffset = HoverOffset.x == 0.0f && HoverOffset.y == 0.0f && PressedOffset.x == 0.0f && PressedOffset.y == 0.0f && FocusOffset.x == 0.0f && FocusOffset.y == 0.0f;
	return bNoScale && bNoOffset;
}

int32 CUIAnimator::FindStateMotion( const CUIWidget& Widget ) const
{
	for ( int32 Index = 0; Index < StateMotions.Num(); ++Index )
	{
		if ( StateMotions[Index].Widget.Pin().Get() == &Widget )
		{
			return Index;
		}
	}
	return -1;
}

bool CUIAnimator::SetStateMotion( const TSharedPtr<CUIWidget>& Widget, const FUIStateMotion& Motion )
{
	if ( !Widget )
	{
		return false;
	}
	const int32 Existing = FindStateMotion( *Widget );
	if ( Existing >= 0 )
	{
		// 元の値は、そのまま。次の Update で、新しい値へ動かし直す
		StateMotions[Existing].Motion = Motion;
		StateMotions[Existing].State = EWidgetState::None;
		return true;
	}
	FStateMotionEntry Entry;
	Entry.Widget = Widget;
	Entry.Motion = Motion;
	Entry.BaseScale = Widget->GetRenderTransform().Scale;
	Entry.BaseTranslation = Widget->GetRenderTransform().Translation;
	StateMotions.Add( Entry );
	return true;
}

bool CUIAnimator::RemoveStateMotion( CUIWidget& Widget )
{
	const int32 Index = FindStateMotion( Widget );
	if ( Index < 0 )
	{
		return false;
	}
	const FStateMotionEntry Entry = StateMotions[Index];
	StateMotions.RemoveAt( Index );
	// 状態の動きが動かしていた値を止めて、元に戻す
	for ( int32 TweenIndex = Tweens.Num() - 1; TweenIndex >= 0; --TweenIndex )
	{
		const FTween& Tween = Tweens[TweenIndex];
		const bool bOwned = Tween.Property == EUIAnimatedProperty::ScaleX || Tween.Property == EUIAnimatedProperty::ScaleY || Tween.Property == EUIAnimatedProperty::TranslationX
							|| Tween.Property == EUIAnimatedProperty::TranslationY;
		if ( !Tween.bCustom && bOwned && Tween.Widget.Pin().Get() == &Widget )
		{
			Tweens.RemoveAt( TweenIndex );
		}
	}
	FUIRenderTransform Transform = Widget.GetRenderTransform();
	Transform.Scale = Entry.BaseScale;
	Transform.Translation = Entry.BaseTranslation;
	Widget.SetRenderTransform( Transform );
	return true;
}

bool CUIAnimator::HasStateMotion( const CUIWidget& Widget ) const
{
	return FindStateMotion( Widget ) >= 0;
}

void CUIAnimator::UpdateStateMotions()
{
	for ( int32 Index = StateMotions.Num() - 1; Index >= 0; --Index )
	{
		const TSharedPtr<CUIWidget> Widget = StateMotions[Index].Widget.Pin();
		if ( !Widget )
		{
			StateMotions.RemoveAt( Index );
			continue;
		}
		EWidgetState State = EWidgetState::Rest;
		if ( Widget->IsEffectivelyEnabled() )
		{
			if ( Widget->IsPressed() )
			{
				State = EWidgetState::Pressed;
			}
			else if ( Widget->IsHovered() )
			{
				State = EWidgetState::Hovered;
			}
			else if ( Widget->IsFocused() )
			{
				State = EWidgetState::Focused;
			}
		}
		if ( State == StateMotions[Index].State )
		{
			continue;
		}
		const bool bFirst = StateMotions[Index].State == EWidgetState::None;
		StateMotions[Index].State = State;
		// 写しを取ってから動かす（Play の中で、配列が動いても、安全なように）
		const FStateMotionEntry Entry = StateMotions[Index];
		if ( bFirst && State == EWidgetState::Rest )
		{
			// 付けた直後で、何の状態でもない。値は、もう元の値なので、動かさない
			continue;
		}
		float32 Scale = 1.0f;
		FVector2 Offset = FVector2( 0.0f, 0.0f );
		if ( State == EWidgetState::Pressed )
		{
			Scale = Entry.Motion.PressedScale;
			Offset = Entry.Motion.PressedOffset;
		}
		else if ( State == EWidgetState::Hovered )
		{
			Scale = Entry.Motion.HoverScale;
			Offset = Entry.Motion.HoverOffset;
		}
		else if ( State == EWidgetState::Focused )
		{
			Scale = Entry.Motion.FocusScale;
			Offset = Entry.Motion.FocusOffset;
		}
		FUIAnimationOptions Options;
		Options.Duration = Entry.Motion.Duration;
		Options.Easing = Entry.Motion.Easing;
		// 使っていない値は、触らない（ほかの動き（出てくるときのずれ など）と、取り合わないように）
		const FUIStateMotion& Motion = Entry.Motion;
		const bool bUsesScale = Motion.HoverScale != 1.0f || Motion.PressedScale != 1.0f || Motion.FocusScale != 1.0f;
		const bool bUsesOffsetX = Motion.HoverOffset.x != 0.0f || Motion.PressedOffset.x != 0.0f || Motion.FocusOffset.x != 0.0f;
		const bool bUsesOffsetY = Motion.HoverOffset.y != 0.0f || Motion.PressedOffset.y != 0.0f || Motion.FocusOffset.y != 0.0f;
		if ( bUsesScale )
		{
			PlayTo( Widget, EUIAnimatedProperty::ScaleX, Entry.BaseScale.x * Scale, Options );
			PlayTo( Widget, EUIAnimatedProperty::ScaleY, Entry.BaseScale.y * Scale, Options );
		}
		if ( bUsesOffsetX )
		{
			PlayTo( Widget, EUIAnimatedProperty::TranslationX, Entry.BaseTranslation.x + Offset.x, Options );
		}
		if ( bUsesOffsetY )
		{
			PlayTo( Widget, EUIAnimatedProperty::TranslationY, Entry.BaseTranslation.y + Offset.y, Options );
		}
	}
}

bool CUIAnimator::Finish( int32 Id )
{
	const int32 Index = FindIndex( Id );
	if ( Index < 0 )
	{
		return false;
	}
	const FTween Tween = Tweens[Index];
	Tweens.RemoveAt( Index );
	if ( Tween.Options.Loop == EUIAnimationLoop::Once )
	{
		if ( ApplyTween( Tween, 1.0f ) && Tween.Options.OnFinished )
		{
			Tween.Options.OnFinished();
		}
	}
	return true;
}

bool CUIAnimator::IsPlaying( int32 Id ) const
{
	return FindIndex( Id ) >= 0;
}

bool CUIAnimator::IsAnimating( const CUIWidget& Widget ) const
{
	for ( int32 Index = 0; Index < Tweens.Num(); ++Index )
	{
		if ( !Tweens[Index].bCustom && Tweens[Index].Widget.Pin().Get() == &Widget )
		{
			return true;
		}
	}
	return false;
}

void CUIAnimator::Update( float32 InDeltaTime )
{
	const float32 DeltaTime = NMath::Max( 0.0f, InDeltaTime );
	// 状態が変わった部品の動きを、先に始める（このフレームから進む）
	UpdateStateMotions();
	// いまある動きの番号を取り出してから進める（進める最中に、Play・Stop されても、混ざらない。このフレームに足された動きは、次のフレームから進む）
	TArray<int32> Ids;
	for ( int32 Index = 0; Index < Tweens.Num(); ++Index )
	{
		Ids.Add( Tweens[Index].Id );
	}
	for ( int32 Step = 0; Step < Ids.Num(); ++Step )
	{
		const int32 Index = FindIndex( Ids[Step] );
		if ( Index < 0 )
		{
			continue;
		}
		Tweens[Index].Elapsed += DeltaTime;
		const FTween Tween = Tweens[Index];
		const float32 Time = Tween.Elapsed - Tween.Options.Delay;
		if ( Time < 0.0f )
		{
			continue;
		}
		const float32 Duration = Tween.Options.Duration;
		float32 Progress = Duration > 0.0f ? Time / Duration : 1.0f;
		bool bFinished = false;
		if ( Tween.Options.Loop == EUIAnimationLoop::Once || Duration <= 0.0f )
		{
			bFinished = Progress >= 1.0f;
			Progress = NMath::Min( Progress, 1.0f );
		}
		else if ( Tween.Options.Loop == EUIAnimationLoop::Loop )
		{
			Progress = Progress - NMath::Floor( Progress );
		}
		else
		{
			// 行き（0〜1）と戻り（1〜2）
			const float32 Cycle = Progress - 2.0f * NMath::Floor( Progress * 0.5f );
			Progress = Cycle <= 1.0f ? Cycle : 2.0f - Cycle;
		}
		if ( bFinished )
		{
			// 先に外す（値を入れる関数・OnFinished の中で、同じ値を Play し直せるように）
			Tweens.RemoveAt( Index );
		}
		const bool bAlive = ApplyTween( Tween, bFinished ? 1.0f : NEasing::Evaluate( Tween.Options.Easing, Progress ) );
		if ( !bAlive && !bFinished )
		{
			// 部品が消えた
			Stop( Tween.Id );
			continue;
		}
		if ( bFinished && bAlive && Tween.Options.OnFinished )
		{
			Tween.Options.OnFinished();
		}
	}
}
