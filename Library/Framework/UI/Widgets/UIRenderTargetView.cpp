// SPDX-License-Identifier: MIT
#include "UIRenderTargetView.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"

void CUIRenderTargetView::SetTargetFactory( const FTargetFactory& InFactory )
{
	TargetFactory = InFactory;
	// 作る関数が替わったら、描画先を作り直して、描き直す
	Target.Reset();
	TargetWidth = 0;
	TargetHeight = 0;
	bRenderRequested = true;
}

void CUIRenderTargetView::SetResolutionScale( float32 InScale )
{
	ResolutionScale = InScale > 0.0f ? InScale : 1.0f;
}

void CUIRenderTargetView::SetMaxTargetSize( int32 InMaxSize )
{
	MaxTargetSize = NMath::Max( 1, InMaxSize );
}

void CUIRenderTargetView::SetUpdateInterval( int32 InFrames )
{
	UpdateInterval = NMath::Max( 0, InFrames );
}

void CUIRenderTargetView::ComputeTargetSize( int32& OutWidth, int32& OutHeight ) const
{
	const FVector2& Size = GetRect().Size;
	if ( Size.x <= 0.0f || Size.y <= 0.0f )
	{
		OutWidth = 0;
		OutHeight = 0;
		return;
	}
	OutWidth = NMath::Clamp( static_cast<int32>( std::ceil( Size.x * ResolutionScale ) ), 1, MaxTargetSize );
	OutHeight = NMath::Clamp( static_cast<int32>( std::ceil( Size.y * ResolutionScale ) ), 1, MaxTargetSize );
}

void CUIRenderTargetView::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( !TargetFactory )
	{
		return;
	}
	int32 Width = 0;
	int32 Height = 0;
	ComputeTargetSize( Width, Height );
	if ( Width <= 0 || Height <= 0 )
	{
		return;
	}

	// 大きさが変わったときだけ、描画先を作り直し、描き直す
	if ( !Target || Width != TargetWidth || Height != TargetHeight )
	{
		Target = TargetFactory( Width, Height );
		TargetWidth = Target ? Width : 0;
		TargetHeight = Target ? Height : 0;
		++CreateCount;
		bRenderRequested = true;
	}
	if ( !Target )
	{
		return;
	}

	// 描き直すか: 要求があったとき、または、更新の間隔が来たとき
	bool bRender = bRenderRequested;
	if ( UpdateInterval > 0 )
	{
		bRender = bRender || ( PaintCounter % UpdateInterval == 0 );
		++PaintCounter;
	}
	if ( bRender )
	{
		if ( RenderFunction )
		{
			{
				FTargetScope Scope( CommandList, Target );
				RenderFunction( CommandList, TargetWidth, TargetHeight );
			}
			// 描く関数が変えたかもしれない状態を、元に戻す
			CommandList.Blend( EDrawBlend::Opaque );
			CommandList.Tint( FColor32( 255, 255, 255 ) );
		}
		bRenderRequested = false;
		++RenderCount;
	}

	// 描いた内容（描かないフレームは、前の内容）を、ブラシで表示する
	const EUIImageMode Mode = ( DisplayMode == EUIImageMode::Fit || DisplayMode == EUIImageMode::Fill ) ? DisplayMode : EUIImageMode::Stretch;
	NUIBrush::Draw( CommandList, FUIBrush::MakeImage( Target, FVector2( static_cast<float32>( TargetWidth ), static_cast<float32>( TargetHeight ) ), Mode, Tint ),
					GetRect().Deflate( GetLayout().Padding ), EffectiveOpacity );
}

FVector2 CUIRenderTargetView::GetViewToScreenScale() const
{
	const FUIRect Box = GetRect().Deflate( GetLayout().Padding );
	const float32 ScaleX = Box.Size.x / static_cast<float32>( TargetWidth );
	const float32 ScaleY = Box.Size.y / static_cast<float32>( TargetHeight );
	if ( DisplayMode == EUIImageMode::Fit )
	{
		const float32 Scale = NMath::Min( ScaleX, ScaleY );
		return FVector2( Scale, Scale );
	}
	if ( DisplayMode == EUIImageMode::Fill )
	{
		const float32 Scale = NMath::Max( ScaleX, ScaleY );
		return FVector2( Scale, Scale );
	}
	return FVector2( ScaleX, ScaleY );
}

bool CUIRenderTargetView::ToViewPosition( const FVector2& ScreenPosition, FVector2& OutViewPosition ) const
{
	OutViewPosition = FVector2( 0.0f, 0.0f );
	const FUIRect Box = GetRect().Deflate( GetLayout().Padding );
	if ( !Target || TargetWidth <= 0 || TargetHeight <= 0 || Box.Size.x <= 0.0f || Box.Size.y <= 0.0f )
	{
		return false;
	}
	// 矩形の中心と、描画先の中心を合わせた、拡大の変換
	const FVector2 Scale = GetViewToScreenScale();
	const FVector2 BoxCenter = Box.GetCenter();
	const FVector2 ViewCenter( static_cast<float32>( TargetWidth ) * 0.5f, static_cast<float32>( TargetHeight ) * 0.5f );
	OutViewPosition = FVector2( ViewCenter.x + ( ScreenPosition.x - BoxCenter.x ) / Scale.x, ViewCenter.y + ( ScreenPosition.y - BoxCenter.y ) / Scale.y );
	return OutViewPosition.x >= 0.0f && OutViewPosition.x < static_cast<float32>( TargetWidth ) && OutViewPosition.y >= 0.0f && OutViewPosition.y < static_cast<float32>( TargetHeight );
}

void CUIRenderTargetView::OnPointerPressed( const FVector2& Position )
{
	FVector2 View;
	ToViewPosition( Position, View );
	OnViewPressed.Broadcast( *this, View );
}

void CUIRenderTargetView::OnPointerDragged( const FVector2& Position )
{
	FVector2 View;
	ToViewPosition( Position, View );
	OnViewDragged.Broadcast( *this, View );
}

void CUIRenderTargetView::OnPointerReleased( const FVector2& Position )
{
	FVector2 View;
	ToViewPosition( Position, View );
	OnViewReleased.Broadcast( *this, View );
}
