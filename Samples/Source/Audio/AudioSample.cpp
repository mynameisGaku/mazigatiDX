#include "AudioSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Audio/PanBall.h"
#include "Source/Audio/SynthPad.h"
#include "Source/Audio/ToneKeys.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CAudioSample> Registrar( FSampleInfo { "Audio", "Audio: tones, pan and a live synth", "Generated sounds: pitch and pan per key, a looping tone that follows a ball, a synth fed to CPcmStream, and the master volume. Silent until you play.", 85 } );
}

void CAudioSample::OnEnter()
{
	if ( const CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
	{
		SavedMasterVolume = Audio->GetMasterVolume();
	}
	SpawnObject<CToneKeys>( FVector2( 170.0f, 130.0f ) );
	SpawnObject<CPanBall>( FVector2( 640.0f, 440.0f ), 380.0f );
	SpawnObject<CSynthPad>( FVector2( 170.0f, 520.0f ), FVector2( 1110.0f, 680.0f ) );
}

void CAudioSample::OnExit()
{
	// 次のシーンに、このサンプルの全体の音量を残さない
	if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
	{
		Audio->SetMasterVolume( SavedMasterVolume );
	}
}

void CAudioSample::OnSampleTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Input == nullptr || Audio == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::Up ) )
	{
		Audio->SetMasterVolume( NMath::Min( 1.0f, Audio->GetMasterVolume() + 0.1f ) );
	}
	if ( Input->IsKeyPressed( EKey::Down ) )
	{
		Audio->SetMasterVolume( NMath::Max( 0.0f, Audio->GetMasterVolume() - 0.1f ) );
	}
}

void CAudioSample::OnSampleDraw( FImmediateDraw& Screen )
{
	const CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	Screen.TextF( 16, 64, FColor32::Gray, "Master volume: %.0f %%  (Up / Down)   Voices: %d", Audio != nullptr ? Audio->GetMasterVolume() * 100.0f : 0.0f, Audio != nullptr ? Audio->GetNumVoices() : 0 );
	Screen.Text( 16, 84, "Keys 1-8 or click: play a note    Enter or click the ball: toggle the looping tone", FColor32::Gray );
}
