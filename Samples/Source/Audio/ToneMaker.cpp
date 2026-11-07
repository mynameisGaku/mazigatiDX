#include "ToneMaker.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Sound/PcmSoundLoader.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Math/Math.h"

FPcmBuffer NToneMaker::MakeNote( float32 Frequency, float32 Seconds, float32 Volume )
{
	FPcmBuffer Pcm;
	Pcm.SampleRate = SampleRate;
	Pcm.Channels = 1;
	const int32 FrameCount = static_cast<int32>( Seconds * static_cast<float32>( SampleRate ) );
	const float32 AttackFrames = 0.01f * static_cast<float32>( SampleRate );
	for ( int32 Frame = 0; Frame < FrameCount; ++Frame )
	{
		const float32 Time = static_cast<float32>( Frame ) / static_cast<float32>( SampleRate );
		const float32 Attack = NMath::Min( 1.0f, static_cast<float32>( Frame ) / AttackFrames );
		// 終わりに向かって、なめらかに小さくなる
		const float32 Decay = 1.0f - static_cast<float32>( Frame ) / static_cast<float32>( FrameCount );
		Pcm.Samples.Add( Volume * Attack * Decay * NMath::Sin( NMath::TWO_PI * Frequency * Time ) );
	}
	return Pcm;
}

FPcmBuffer NToneMaker::MakeLoopTone( float32 Frequency, float32 Volume )
{
	FPcmBuffer Pcm;
	Pcm.SampleRate = SampleRate;
	Pcm.Channels = 1;
	// 1 秒に Frequency 回の波（Frequency は整数の Hz にする）→ 1 秒でちょうど波が閉じる
	for ( int32 Frame = 0; Frame < SampleRate; ++Frame )
	{
		const float32 Time = static_cast<float32>( Frame ) / static_cast<float32>( SampleRate );
		Pcm.Samples.Add( Volume * NMath::Sin( NMath::TWO_PI * Frequency * Time ) );
	}
	return Pcm;
}

TSharedPtr<CSound> NToneMaker::MakeSound( const char* Name, const FPcmBuffer& Pcm )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return nullptr;
	}
	CPcmSoundLoader Loader( Assets->GetBackend() );
	return Loader.CreateFromPcm( Name, Pcm );
}
