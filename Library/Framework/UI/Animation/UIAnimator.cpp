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
		const bool bAlive = ApplyTween( Tween, bFinished ? 1.0f : NUIEasing::Evaluate( Tween.Options.Easing, Progress ) );
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
