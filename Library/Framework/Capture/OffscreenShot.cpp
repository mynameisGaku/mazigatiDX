// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/OffscreenShot.h"

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Framework/Capture/AlphaMatte.h"
#include "Library/Framework/Scene/ISceneManager.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 MaxSize = 16384;

	// 1 回描いて、画素を読む
	bool RenderOnce( IDrawExecutor& Executor, ISceneManager& Scenes, int32 Width, int32 Height, float32 Scale, const FColor32& Clear, const TCallable<bool( const CGameObject& )>* Subject, TArray<FColor32>& Out )
	{
		FOffscreenCaptureDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.Scale = Scale;
		Desc.ClearColor = Clear;
		if ( !Executor.BeginOffscreenCapture( Desc ) )
		{
			return false;
		}
		Scenes.RedrawScenesForCapture( Subject );
		return Executor.EndOffscreenCapture( Out ) && Out.Num() == Width * Height;
	}
}

bool NOffscreenShot::ResolveSize( const FHighResShotOptions& Options, int32 ScreenWidth, int32 ScreenHeight, int32& OutWidth, int32& OutHeight, float32& OutScale )
{
	if ( ScreenWidth <= 0 || ScreenHeight <= 0 )
	{
		return false;
	}
	if ( Options.Width > 0 && Options.Height > 0 )
	{
		OutWidth = Options.Width;
		OutHeight = Options.Height;
		// 2D は、縦横どちらにも収まる倍率で描く
		OutScale = NMath::Min( static_cast<float32>( Options.Width ) / static_cast<float32>( ScreenWidth ), static_cast<float32>( Options.Height ) / static_cast<float32>( ScreenHeight ) );
	}
	else
	{
		if ( !( Options.Multiplier > 0.0f ) )
		{
			return false;
		}
		OutScale = Options.Multiplier;
		OutWidth = static_cast<int32>( static_cast<float32>( ScreenWidth ) * Options.Multiplier + 0.5f );
		OutHeight = static_cast<int32>( static_cast<float32>( ScreenHeight ) * Options.Multiplier + 0.5f );
	}
	return OutWidth >= 1 && OutHeight >= 1 && OutWidth <= MaxSize && OutHeight <= MaxSize && OutScale > 0.0f;
}

bool NOffscreenShot::Render( IDrawExecutor& Executor, ISceneManager& Scenes, int32 Width, int32 Height, float32 Scale, const TCallable<bool( const CGameObject& )>* Subject, TArray<FColor32>& Out )
{
	if ( Subject == nullptr )
	{
		if ( !RenderOnce( Executor, Scenes, Width, Height, Scale, FColor32( 0, 0, 0, 255 ), nullptr, Out ) )
		{
			return false;
		}
		for ( FColor32& Pixel : Out )
		{
			Pixel.a = 255;
		}
		return true;
	}
	TArray<FColor32> OnBlack;
	TArray<FColor32> OnWhite;
	if ( !RenderOnce( Executor, Scenes, Width, Height, Scale, FColor32( 0, 0, 0, 255 ), Subject, OnBlack ) || !RenderOnce( Executor, Scenes, Width, Height, Scale, FColor32( 255, 255, 255, 255 ), Subject, OnWhite ) )
	{
		return false;
	}
	return NAlphaMatte::FromBlackAndWhite( OnBlack, OnWhite, Out );
}
