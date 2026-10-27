#include "AudioTypes.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

const char* ToString( EAudioBus Bus )
{
	switch ( Bus )
	{
	case EAudioBus::Master: return "Master";
	case EAudioBus::Music: return "Music";
	case EAudioBus::Sfx: return "Sfx";
	case EAudioBus::Ambient: return "Ambient";
	case EAudioBus::Voice: return "Voice";
	case EAudioBus::Ui: return "Ui";
	default: return "?";
	}
}

bool TryParse( const FString& Text, EAudioBus& Out )
{
	for ( int32 i = 0; i < static_cast<int32>( EAudioBus::Count ); ++i )
	{
		const EAudioBus Bus = static_cast<EAudioBus>( i );
		if ( Text.Equals( ToString( Bus ), ESearchCase::IgnoreCase ) )
		{
			Out = Bus;
			return true;
		}
	}
	return false;
}

//=============================================================================
// FAudioFade
//=============================================================================
void FAudioFade::Reset( float32 V )
{
	Value = V;
	From = V;
	To = V;
	Elapsed = 0.0f;
	Duration = 0.0f;
}

void FAudioFade::FadeTo( float32 Target, float32 Seconds, EFadeCurve InCurve )
{
	if ( Seconds <= 0.0f )
	{
		Reset( Target );
		return;
	}
	From = Value;
	To = Target;
	Elapsed = 0.0f;
	Duration = Seconds;
	Curve = InCurve;
}

void FAudioFade::Step( float32 DeltaSeconds )
{
	if ( Elapsed >= Duration ) return;

	Elapsed += DeltaSeconds;
	if ( Elapsed >= Duration )
	{
		Elapsed = Duration;
		Value = To;
		return;
	}
	const float32 Progress = NAudio::ShapeFade( Curve, Elapsed / Duration, To >= From );
	Value = From + ( To - From ) * Progress;
}

//=============================================================================
// NAudio
//=============================================================================
float32 NAudio::ShapeFade( EFadeCurve Curve, float32 T, bool bRising )
{
	T = NMath::Clamp( T, 0.0f, 1.0f );
	switch ( Curve )
	{
	case EFadeCurve::Linear: return T;
	case EFadeCurve::EaseIn: return T * T;
	case EFadeCurve::EaseOut: return 1.0f - ( 1.0f - T ) * ( 1.0f - T );
	case EFadeCurve::SmoothStep: return T * T * ( 3.0f - 2.0f * T );
	case EFadeCurve::EqualPower:
	{
		constexpr float32 HalfPi = 1.57079632679f;
		// 上がる側は sin(t)、下がる側は cos(t)（値 = From * cos なので、変化の割合は 1 - cos）
		return bRising ? NMath::Sin( T * HalfPi ) : 1.0f - NMath::Cos( T * HalfPi );
	}
	}
	return T;
}

float32 NAudio::RolloffGain( EAudioRolloff Rolloff, float32 Distance, float32 MinDistance, float32 MaxDistance )
{
	if ( MaxDistance <= MinDistance ) return Distance <= MinDistance ? 1.0f : 0.0f;
	if ( Distance <= MinDistance ) return 1.0f;
	if ( Distance >= MaxDistance ) return 0.0f;

	const float32 T = ( Distance - MinDistance ) / ( MaxDistance - MinDistance );   // 0..1
	switch ( Rolloff )
	{
	case EAudioRolloff::Linear: return 1.0f - T;
	case EAudioRolloff::Squared: return ( 1.0f - T ) * ( 1.0f - T );
	case EAudioRolloff::Inverse:
	{
		// MinDistance / Distance。MaxDistance で 0 になるよう、そこでの値を引いて整える
		const float32 AtMax = MinDistance / MaxDistance;
		const float32 Raw = MinDistance > 0.0f ? MinDistance / Distance : 1.0f - T;
		return NMath::Clamp( ( Raw - AtMax ) / ( 1.0f - AtMax ), 0.0f, 1.0f );
	}
	}
	return 1.0f - T;
}

float32 NAudio::SpatialPan( const FVector3& ToSource, const FVector3& Right, float32 Strength )
{
	const float32 Length = ToSource.Length();
	if ( Length < 1.0e-4f ) return 0.0f;   // 真上（リスナーの位置）では左右が決まらない
	const float32 Side = FVector3::Dot( ToSource / Length, Right );
	return NMath::Clamp( Side * Strength, -1.0f, 1.0f );
}

FVector3 NAudio::ListenerRight( const FAudioListener& Listener )
{
	const FVector3 Right = FVector3::Cross( Listener.Up, Listener.Forward );
	return Right.LengthSquared() > 1.0e-8f ? Right.Normalized() : FVector3::Right;
}

int32 NAudio::VolumeToDecibel100( float32 Volume )
{
	if ( Volume <= 1.0e-5f ) return MinDecibel100;
	return NMath::Max( MinDecibel100, static_cast<int32>( NMath::RoundToInt( 2000.0f * NMath::Log10( Volume ) ) ) );
}
