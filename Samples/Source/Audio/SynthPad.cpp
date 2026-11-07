#include "SynthPad.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Audio/PcmStream.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Audio/ToneMaker.h"

namespace
{
	// 横の位置で決まる周波数の範囲（Hz）と、いちばん大きいときの音量（小さめ）
	constexpr float32 MinFrequency = 200.0f;
	constexpr float32 MaxFrequency = 800.0f;
	constexpr float32 MaxVolume = 0.2f;

	// 残りがこの秒数を切ったら足す。1 回に足す長さ
	constexpr float32 QueueTarget = 0.15f;
	constexpr float32 FeedChunk = 0.05f;
}

CSynthPad::CSynthPad( const FVector2& InMin, const FVector2& InMax )
	: Min( InMin )
	, Max( InMax )
{}

void CSynthPad::OnSpawn()
{
	if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
	{
		Stream = Audio->CreatePcmStream( NToneMaker::SampleRate, 1 );
	}
}

void CSynthPad::Feed( float32 Seconds )
{
	const int32 FrameCount = static_cast<int32>( Seconds * static_cast<float32>( NToneMaker::SampleRate ) );
	TArray<float32> Samples;
	for ( int32 Frame = 0; Frame < FrameCount; ++Frame )
	{
		Samples.Add( Volume * NMath::Sin( NMath::TWO_PI * Phase ) );
		Phase += Frequency / static_cast<float32>( NToneMaker::SampleRate );
		if ( Phase >= 1.0f )
		{
			Phase -= 1.0f;
		}
	}
	Stream->Submit( Samples.GetData(), FrameCount );
}

void CSynthPad::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr || Stream == nullptr || !Stream->IsValid() )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	const bool bInside = Mouse.X >= Min.x && Mouse.X <= Max.x && Mouse.Y >= Min.y && Mouse.Y <= Max.y;
	const bool bDown = Input->IsMouseButtonDown( EMouseButton::Left );

	if ( bInside && bDown )
	{
		const float32 X = ( static_cast<float32>( Mouse.X ) - Min.x ) / ( Max.x - Min.x );
		const float32 Y = ( static_cast<float32>( Mouse.Y ) - Min.y ) / ( Max.y - Min.y );
		Frequency = MinFrequency + ( MaxFrequency - MinFrequency ) * X;
		Volume = MaxVolume * ( 1.0f - Y );
		if ( !bActive )
		{
			bActive = true;
			Stream->Start();
		}
		// 残りが少なくなったら、少し先まで足す
		while ( Stream->GetQueuedSeconds() < QueueTarget )
		{
			Feed( FeedChunk );
		}
	}
	else if ( bActive )
	{
		bActive = false;
		Stream->Reset();
	}
}

void CSynthPad::Draw( FDrawContext& CommandList )
{
	const int32 Left = static_cast<int32>( Min.x );
	const int32 Top = static_cast<int32>( Min.y );
	const int32 Right = static_cast<int32>( Max.x );
	const int32 Bottom = static_cast<int32>( Max.y );
	CommandList.Box( Left, Top, Right, Bottom, bActive ? FColor32( 50, 80, 70 ) : FColor32( 40, 44, 56 ), true );
	CommandList.Box( Left, Top, Right, Bottom, FColor32( 120, 126, 140 ), false, 2 );
	if ( bActive )
	{
		const float32 X = ( Frequency - MinFrequency ) / ( MaxFrequency - MinFrequency );
		const int32 MarkX = Left + static_cast<int32>( X * static_cast<float32>( Right - Left ) );
		CommandList.Line( MarkX, Top, MarkX, Bottom, FColor32( 90, 200, 140 ), 2 );
		CommandList.TextF( Left + 8, Bottom - 24, FColor32::White, "%.0f Hz   volume %.2f", Frequency, Volume );
	}
	else
	{
		CommandList.Text( Left + 8, Top + 8, "Synth pad: hold the left button and drag", FColor32::Gray );
		CommandList.Text( Left + 8, Top + 28, "left = low, right = high, up = loud, down = quiet", FColor32::Gray );
	}
}

void CSynthPad::OnDestroy()
{
	if ( Stream != nullptr && Stream->IsValid() )
	{
		Stream->Reset();
	}
	Stream.Reset();
}
