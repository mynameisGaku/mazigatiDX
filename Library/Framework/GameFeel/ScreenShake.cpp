// SPDX-License-Identifier: MIT
#include "ScreenShake.h"

#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 TwoPi = 6.2831853f;

	bool IsUsable( float32 Value )
	{
		return NMath::IsFinite( Value ) && Value > 0.0f;
	}

	// 収まっていく包み（1 → 0。最初が強く、終わりは、なだらか）
	float32 Envelope( float32 Elapsed, float32 Duration )
	{
		if ( Duration <= 0.0f || Elapsed >= Duration )
		{
			return 0.0f;
		}
		const float32 Left = 1.0f - Elapsed / Duration;
		return Left * Left;
	}
}

FScreenShakeSettings FScreenShakeSettings::Make( EScreenShakeStrength Strength )
{
	FScreenShakeSettings Settings;
	switch ( Strength )
	{
	case EScreenShakeStrength::Small:
		Settings.Duration = 0.15f;
		Settings.AmplitudeX = 0.0f;
		Settings.AmplitudeY = 5.0f;
		Settings.Frequency = 30.0f;
		break;
	case EScreenShakeStrength::Medium:
		Settings.Duration = 0.25f;
		Settings.AmplitudeX = 6.0f;
		Settings.AmplitudeY = 9.0f;
		Settings.Frequency = 28.0f;
		break;
	default:
		Settings.Duration = 0.45f;
		Settings.AmplitudeX = 14.0f;
		Settings.AmplitudeY = 18.0f;
		Settings.Frequency = 24.0f;
		break;
	}
	return Settings;
}

void FScreenShake::Start( EScreenShakeStrength Strength, float32 Scale )
{
	Start( FScreenShakeSettings::Make( Strength ), Scale );
}

void FScreenShake::Start( const FScreenShakeSettings& InSettings, float32 Scale )
{
	if ( !IsUsable( InSettings.Duration ) || !IsUsable( InSettings.Frequency ) || !IsUsable( Scale ) || !NMath::IsFinite( InSettings.AmplitudeX ) || !NMath::IsFinite( InSettings.AmplitudeY ) )
	{
		return;
	}
	FScreenShakeSettings Scaled = InSettings;
	Scaled.AmplitudeX = NMath::Abs( InSettings.AmplitudeX ) * Scale;
	Scaled.AmplitudeY = NMath::Abs( InSettings.AmplitudeY ) * Scale;
	const float32 Wanted = NMath::Max( Scaled.AmplitudeX, Scaled.AmplitudeY );
	// いまの揺れのほうが大きければ、そのまま（小さい揺れで、大きい揺れを消さない）
	if ( Wanted <= 0.0f || Wanted < GetCurrentAmplitude() )
	{
		return;
	}
	Settings = Scaled;
	Elapsed = 0.0f;
	++Variation;
}

void FScreenShake::Stop()
{
	Elapsed = Settings.Duration;
}

void FScreenShake::Advance( float32 DeltaTime )
{
	if ( !NMath::IsFinite( DeltaTime ) || DeltaTime <= 0.0f || !IsActive() )
	{
		return;
	}
	Elapsed = NMath::Min( Elapsed + DeltaTime, Settings.Duration );
}

float32 FScreenShake::GetCurrentAmplitude() const
{
	return NMath::Max( Settings.AmplitudeX, Settings.AmplitudeY ) * Envelope( Elapsed, Settings.Duration );
}

FVector2 FScreenShake::GetOffset() const
{
	const float32 Strength = Envelope( Elapsed, Settings.Duration );
	if ( Strength <= 0.0f )
	{
		return FVector2( 0.0f, 0.0f );
	}
	// 縦は、始まった瞬間から大きくずれる（コサイン）。横は、少し違う速さで揺らして、同じ道を往復しないようにする。
	// 揺れごとに、横の向きと、揺れの細かな形を変える（毎回同じ揺れに見えないように）
	const float32 Phase = TwoPi * Settings.Frequency * Elapsed;
	const float32 Seed = static_cast<float32>( Variation % 7u ) * 0.9f;
	const float32 Wobble = 0.8f + 0.2f * NMath::Cos( Phase * 0.37f + Seed );
	const float32 Side = ( Variation % 2u ) == 0u ? 1.0f : -1.0f;
	return FVector2( Settings.AmplitudeX * Strength * Side * NMath::Cos( Phase * 1.31f + Seed ) * Wobble, Settings.AmplitudeY * Strength * NMath::Cos( Phase ) * Wobble );
}
