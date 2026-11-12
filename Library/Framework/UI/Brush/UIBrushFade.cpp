// SPDX-License-Identifier: MIT
#include "UIBrushFade.h"

#include "Library/Math/MathUtils.h"

void FUIStateFade::Advance( int32 Target, float32 DeltaTime, float32 Duration )
{
	if ( Target < 0 )
	{
		return;
	}
	if ( Current < 0 || !( Duration > 0.0f ) )
	{
		Current = Target;
		Previous = -1;
		Progress = 1.0f;
		return;
	}
	if ( Target != Current )
	{
		if ( Target == Previous && Progress < 1.0f )
		{
			// 途中で、前の状態へ戻る: 進んだ分だけ戻る
			Previous = Current;
			Current = Target;
			Progress = 1.0f - Progress;
		}
		else
		{
			Previous = Current;
			Current = Target;
			Progress = 0.0f;
		}
	}
	if ( Previous >= 0 )
	{
		Progress = NMath::Min( 1.0f, Progress + ( DeltaTime > 0.0f ? DeltaTime : 0.0f ) / Duration );
		if ( Progress >= 1.0f )
		{
			Previous = -1;
		}
	}
}

void FUIOverlayFade::Advance( bool bShown, float32 DeltaTime, float32 Duration )
{
	if ( !bStarted || !( Duration > 0.0f ) )
	{
		bStarted = true;
		Amount = bShown ? 1.0f : 0.0f;
		return;
	}
	const float32 Step = ( DeltaTime > 0.0f ? DeltaTime : 0.0f ) / Duration;
	Amount = bShown ? NMath::Min( 1.0f, Amount + Step ) : NMath::Max( 0.0f, Amount - Step );
}

void NUIBrush::DrawFade( FDrawContext& CommandList, const FUIBrush& Previous, const FUIBrush& Current, float32 Progress, const FUIRect& Rect, float32 Opacity )
{
	const float32 Amount = Progress > 0.0f ? NMath::Min( Progress, 1.0f ) : 0.0f;
	if ( Current.IsEmpty() )
	{
		Draw( CommandList, Previous, Rect, Opacity * ( 1.0f - Amount ) );
		return;
	}
	if ( Amount < 1.0f )
	{
		Draw( CommandList, Previous, Rect, Opacity );
	}
	Draw( CommandList, Current, Rect, Opacity * Amount );
}
