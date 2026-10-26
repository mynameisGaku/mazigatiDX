#include "AudioSystem.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

DEFINE_LOG_CATEGORY( LogAudio )

REGISTER_SUBSYSTEM( CAudioSystem )

namespace
{
	constexpr float32 StealFadeSeconds = 0.03f;   // 数の制限で古い音を止めるときの短いフェード（ぷつっという音を避ける）
	constexpr float32 ChangeEpsilon = 1.0e-4f;
}

CAudioSystem::CAudioSystem() = default;
CAudioSystem::~CAudioSystem() = default;

//=============================================================================
// 生成・破棄
//=============================================================================
void CAudioSystem::Initialize( FSubsystemCollection& Collection )
{
	for ( FBus& Bus : Buses ) Bus.Volume.Reset( 1.0f );

	Backend = Collection.InitializeDependency<IAudioBackend>();
	if ( Backend == nullptr )
	{
		LOG( LogAudio, Warning, "No audio backend is available. Sounds will not be played." );
		return;
	}
	LOG( LogAudio, Log, "Audio ready (max %d voice(s), %d bus(es))", MaxVoices, static_cast<int32>( EAudioBus::Count ) );
}

void CAudioSystem::Deinitialize()
{
	// 鳴らしている音を返す（CSound が先に壊れても困らないように）
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( Voices[Index].bInUse ) FreeVoice( Index );
	}
	Voices.Empty();
	LastPlayTime.Empty();
	Backend = nullptr;
}

void CAudioSystem::SetBackendOverride( IAudioBackend* InBackend )
{
	// 差し替える前に、古い方で鳴らしていた音を片付ける
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( Voices[Index].bInUse ) FreeVoice( Index );
	}
	BackendOverride = InBackend;
}

//=============================================================================
// ハンドル
//=============================================================================
FSoundHandle CAudioSystem::MakeHandle( int32 Index ) const
{
	FSoundHandle Handle;
	Handle.Id = ( Voices[Index].Generation << 16 ) | static_cast<uint32>( Index + 1 );
	return Handle;
}

CAudioSystem::FVoice* CAudioSystem::Resolve( FSoundHandle Handle )
{
	if ( !Handle.IsValid() ) return nullptr;
	const int32 Index = static_cast<int32>( Handle.Id & 0xFFFFu ) - 1;
	if ( !Voices.IsValidIndex( Index ) ) return nullptr;
	FVoice& Voice = Voices[Index];
	return Voice.bInUse && Voice.Generation == ( Handle.Id >> 16 ) ? &Voice : nullptr;
}

const CAudioSystem::FVoice* CAudioSystem::Resolve( FSoundHandle Handle ) const
{
	return const_cast<CAudioSystem*>( this )->Resolve( Handle );
}

//=============================================================================
// 鳴らす
//=============================================================================
float32 CAudioSystem::NextRandom()
{
	// xorshift32。同じ種なら同じ並び
	RandomState ^= RandomState << 13;
	RandomState ^= RandomState >> 17;
	RandomState ^= RandomState << 5;
	return static_cast<float32>( RandomState ) / 2147483648.0f - 1.0f;
}

bool CAudioSystem::AdmitNewVoice( const TSharedPtr<CSound>& Sound, const FSoundPlayParams& Params )
{
	if ( Params.MinInterval > 0.0f )
	{
		if ( const float64* Last = LastPlayTime.Find( Sound.Get() ) )
		{
			if ( Clock - *Last < static_cast<float64>( Params.MinInterval ) ) return false;
		}
	}

	if ( Params.MaxInstances > 0 )
	{
		int32 Count = 0;
		int32 Oldest = INDEX_NONE;
		for ( int32 Index = 0; Index < Voices.Num(); ++Index )
		{
			const FVoice& Voice = Voices[Index];
			if ( !Voice.bInUse || Voice.Sound != Sound || Voice.bStopWhenFaded ) continue;
			++Count;
			if ( Oldest == INDEX_NONE || Voice.StartTime < Voices[Oldest].StartTime ) Oldest = Index;
		}
		if ( Count >= Params.MaxInstances )
		{
			if ( Params.Overflow == EAudioOverflow::IgnoreNew ) return false;
			if ( Sound->IsStream() )
			{
				FreeVoice( Oldest );   // 複製できない音は、すぐ空けないと次を鳴らせない
			}
			else
			{
				FVoice& Victim = Voices[Oldest];
				Victim.Gain.FadeTo( 0.0f, StealFadeSeconds, EFadeCurve::Linear );
				Victim.bStopWhenFaded = true;
			}
		}
	}
	return true;
}

int32 CAudioSystem::AllocateSlot( int32 NewPriority )
{
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( !Voices[Index].bInUse ) return Index;
	}
	if ( Voices.Num() < MaxVoices ) return Voices.AddDefaulted();

	// 一杯: 優先度が低い、ループしていない、小さい、古い順に止めてよいものを探す
	int32 Victim = INDEX_NONE;
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		const FVoice& Candidate = Voices[Index];
		if ( Victim == INDEX_NONE ) { Victim = Index; continue; }
		const FVoice& Best = Voices[Victim];
		if ( Candidate.Priority != Best.Priority ) { if ( Candidate.Priority < Best.Priority ) Victim = Index; continue; }
		if ( Candidate.bLoop != Best.bLoop ) { if ( !Candidate.bLoop ) Victim = Index; continue; }
		if ( NMath::Abs( Candidate.FinalVolume - Best.FinalVolume ) > ChangeEpsilon ) { if ( Candidate.FinalVolume < Best.FinalVolume ) Victim = Index; continue; }
		if ( Candidate.StartTime < Best.StartTime ) Victim = Index;
	}
	if ( Victim == INDEX_NONE || Voices[Victim].Priority > NewPriority ) return INDEX_NONE;

	LOG( LogAudio, Verbose, "Voice limit (%d) reached: stopped '%s' to make room.", MaxVoices, Voices[Victim].Sound.IsValid() ? *Voices[Victim].Sound->GetPath() : "?" );
	FreeVoice( Victim );
	return Victim;
}

void CAudioSystem::FreeVoice( int32 Index )
{
	FVoice& Voice = Voices[Index];
	if ( !Voice.bInUse ) return;

	if ( IAudioBackend* B = GetBackend() ) B->Stop( Voice.BackendVoice );
	if ( Voice.Sound.IsValid() ) Voice.Sound->ReleaseVoice( Voice.BackendVoice );

	const uint32 NextGeneration = Voice.Generation + 1 > 0xFFFFu ? 1u : Voice.Generation + 1;
	Voice = FVoice();
	Voice.Generation = NextGeneration;
	--NumActive;
}

FSoundHandle CAudioSystem::Play( const TSharedPtr<CSound>& Sound, const FSoundPlayParams& Params )
{
	PROFILE_SCOPE_CAT( "Audio", "Play" );
	IAudioBackend* B = GetBackend();
	if ( B == nullptr ) return FSoundHandle();
	if ( !Sound.IsValid() )
	{
		LOG( LogAudio, Warning, "Play: no sound was given." );
		return FSoundHandle();
	}
	if ( !Sound->IsReady() )
	{
		LOG( LogAudio, Warning, "Play: the sound '%s' is not loaded yet.", *Sound->GetPath() );
		return FSoundHandle();
	}
	if ( !AdmitNewVoice( Sound, Params ) ) return FSoundHandle();

	const int32 Slot = AllocateSlot( Params.Priority );
	if ( Slot == INDEX_NONE )
	{
		LOG( LogAudio, Verbose, "Play: no free voice for '%s' (all %d voice(s) have a higher priority).", *Sound->GetPath(), MaxVoices );
		return FSoundHandle();
	}

	const int32 BackendVoice = Sound->AcquireVoice();
	if ( BackendVoice < 0 )
	{
		LOG( LogAudio, Verbose, "Play: no voice is available for '%s'%s.", *Sound->GetPath(), Sound->IsStream() ? " (a streamed sound can only play once at a time)" : "" );
		return FSoundHandle();
	}

	FVoice& Voice = Voices[Slot];
	const uint32 Generation = Voice.Generation;
	Voice = FVoice();
	Voice.Generation = Generation;
	Voice.bInUse = true;
	Voice.Sound = Sound;
	Voice.BackendVoice = BackendVoice;
	Voice.Bus = Params.Bus;
	Voice.Priority = Params.Priority;
	Voice.bLoop = Params.bLoop;
	Voice.bPausable = Params.bPausable;
	Voice.StartTime = Clock;

	float32 Volume = Params.Volume;
	float32 Pitch = Params.Pitch;
	if ( Params.VolumeVariation > 0.0f ) Volume *= 1.0f + Params.VolumeVariation * NextRandom();
	if ( Params.PitchVariation > 0.0f ) Pitch *= 1.0f + Params.PitchVariation * NextRandom();
	Voice.Volume.Reset( NMath::Max( Volume, 0.0f ) );
	Voice.Pitch.Reset( NMath::Max( Pitch, 0.01f ) );
	Voice.Pan.Reset( NMath::Clamp( Params.Pan, -1.0f, 1.0f ) );
	Voice.Gain.Reset( 1.0f );
	if ( Params.FadeInTime > 0.0f )
	{
		Voice.Gain.Reset( 0.0f );
		Voice.Gain.FadeTo( 1.0f, Params.FadeInTime, Params.FadeInCurve );
	}

	Voice.bSpatial = Params.bSpatial;
	Voice.Position = Params.Position;
	Voice.MinDistance = Params.MinDistance;
	Voice.MaxDistance = Params.MaxDistance;
	Voice.Rolloff = Params.Rolloff;
	Voice.PanStrength = NMath::Clamp( Params.SpatialPanStrength, 0.0f, 1.0f );

	++NumActive;
	LastPlayTime.Add( Sound.Get(), Clock );

	// 音が出る前に、音量・パン・ピッチを決めておく（出だしで音量が一瞬跳ねない）
	Apply( Voice, true );
	if ( Params.StartTime > 0.0f ) B->SetPlayPosition( BackendVoice, Params.StartTime );

	if ( ShouldBePaused( Voice ) )
	{
		// 一時停止中に鳴らした音は、再開したときに始まる
		B->SetPlayPosition( BackendVoice, Params.StartTime );
		Voice.bPaused = true;
	}
	else if ( !B->Play( BackendVoice, Params.bLoop, Params.StartTime <= 0.0f ) )
	{
		LOG( LogAudio, Warning, "Play: the backend failed to start '%s'.", *Sound->GetPath() );
		FreeVoice( Slot );
		return FSoundHandle();
	}

	return MakeHandle( Slot );
}

FSoundHandle CAudioSystem::PlayAt( const TSharedPtr<CSound>& Sound, const FVector3& Position, const FSoundPlayParams& Params )
{
	FSoundPlayParams Spatial = Params;
	Spatial.bSpatial = true;
	Spatial.Position = Position;
	return Play( Sound, Spatial );
}

void CAudioSystem::Stop( FSoundHandle Handle, float32 FadeOut, EFadeCurve Curve )
{
	FVoice* Voice = Resolve( Handle );
	if ( Voice == nullptr ) return;

	if ( FadeOut <= 0.0f || Voice->bPaused )
	{
		FreeVoice( static_cast<int32>( Voice - Voices.GetData() ) );
		return;
	}
	Voice->Gain.FadeTo( 0.0f, FadeOut, Curve );
	Voice->bStopWhenFaded = true;
}

void CAudioSystem::StopAll( float32 FadeOut )
{
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( Voices[Index].bInUse ) Stop( MakeHandle( Index ), FadeOut );
	}
}

void CAudioSystem::StopBus( EAudioBus Bus, float32 FadeOut )
{
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( Voices[Index].bInUse && Voices[Index].Bus == Bus ) Stop( MakeHandle( Index ), FadeOut );
	}
}

void CAudioSystem::StopSound( const TSharedPtr<CSound>& Sound, float32 FadeOut )
{
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		if ( Voices[Index].bInUse && Voices[Index].Sound == Sound ) Stop( MakeHandle( Index ), FadeOut );
	}
}

bool CAudioSystem::IsPlaying( FSoundHandle Handle ) const
{
	const FVoice* Voice = Resolve( Handle );
	return Voice != nullptr && !Voice->bStopWhenFaded;
}

//=============================================================================
// 鳴らしている音の操作
//=============================================================================
void CAudioSystem::SetVolume( FSoundHandle Handle, float32 Volume, float32 FadeTime, EFadeCurve Curve )
{
	if ( FVoice* Voice = Resolve( Handle ) ) Voice->Volume.FadeTo( NMath::Max( Volume, 0.0f ), FadeTime, Curve );
}

void CAudioSystem::SetPitch( FSoundHandle Handle, float32 Pitch, float32 FadeTime, EFadeCurve Curve )
{
	if ( FVoice* Voice = Resolve( Handle ) ) Voice->Pitch.FadeTo( NMath::Max( Pitch, 0.01f ), FadeTime, Curve );
}

void CAudioSystem::SetPan( FSoundHandle Handle, float32 Pan, float32 FadeTime, EFadeCurve Curve )
{
	if ( FVoice* Voice = Resolve( Handle ) ) Voice->Pan.FadeTo( NMath::Clamp( Pan, -1.0f, 1.0f ), FadeTime, Curve );
}

void CAudioSystem::SetPosition( FSoundHandle Handle, const FVector3& Position )
{
	if ( FVoice* Voice = Resolve( Handle ) )
	{
		Voice->bSpatial = true;
		Voice->Position = Position;
	}
}

void CAudioSystem::Pause( FSoundHandle Handle )
{
	if ( FVoice* Voice = Resolve( Handle ) )
	{
		Voice->bUserPaused = true;
		ApplyPauseState();
	}
}

void CAudioSystem::Resume( FSoundHandle Handle )
{
	if ( FVoice* Voice = Resolve( Handle ) )
	{
		Voice->bUserPaused = false;
		ApplyPauseState();
	}
}

//=============================================================================
// バス・一時停止
//=============================================================================
void CAudioSystem::SetBusVolume( EAudioBus Bus, float32 Volume, float32 FadeTime, EFadeCurve Curve )
{
	Buses[static_cast<int32>( Bus )].Volume.FadeTo( NMath::Max( Volume, 0.0f ), FadeTime, Curve );
}

float32 CAudioSystem::GetBusVolume( EAudioBus Bus ) const
{
	return Buses[static_cast<int32>( Bus )].Volume.Value;
}

void CAudioSystem::SetBusMuted( EAudioBus Bus, bool bMuted )
{
	Buses[static_cast<int32>( Bus )].bMuted = bMuted;
}

bool CAudioSystem::IsBusMuted( EAudioBus Bus ) const
{
	return Buses[static_cast<int32>( Bus )].bMuted;
}

float32 CAudioSystem::GetBusGain( EAudioBus Bus ) const
{
	auto Gain = [this]( EAudioBus B ) {
		const FBus& State = Buses[static_cast<int32>( B )];
		return State.bMuted ? 0.0f : State.Volume.Value;
	};
	return Bus == EAudioBus::Master ? Gain( Bus ) : Gain( Bus ) * Gain( EAudioBus::Master );
}

bool CAudioSystem::ShouldBePaused( const FVoice& Voice ) const
{
	if ( Voice.bUserPaused ) return true;
	return Voice.bPausable && ( bGlobalPaused || Buses[static_cast<int32>( Voice.Bus )].bPaused );
}

void CAudioSystem::ApplyPauseState()
{
	IAudioBackend* B = GetBackend();
	if ( B == nullptr ) return;

	for ( FVoice& Voice : Voices )
	{
		if ( !Voice.bInUse ) continue;
		const bool bWant = ShouldBePaused( Voice );
		if ( bWant && !Voice.bPaused )
		{
			B->Stop( Voice.BackendVoice );
			Voice.bPaused = true;
		}
		else if ( !bWant && Voice.bPaused )
		{
			Voice.bPaused = false;
			B->Play( Voice.BackendVoice, Voice.bLoop, false );   // 止めた位置から続ける
		}
	}
}

void CAudioSystem::SetPaused( bool bPaused )
{
	if ( bGlobalPaused == bPaused ) return;
	bGlobalPaused = bPaused;
	ApplyPauseState();
}

void CAudioSystem::SetBusPaused( EAudioBus Bus, bool bPaused )
{
	Buses[static_cast<int32>( Bus )].bPaused = bPaused;
	ApplyPauseState();
}

//=============================================================================
// 音楽・環境音
//=============================================================================
FSoundHandle CAudioSystem::PlayMusic( const TSharedPtr<CSound>& Music, const FMusicParams& Params )
{
	if ( !Music.IsValid() ) return FSoundHandle();

	// すでに同じ音楽が鳴っているなら、音量だけ合わせる
	if ( Resolve( MusicHandle ) != nullptr && MusicSound.Pin() == Music && !Params.bRestartIfSame )
	{
		SetVolume( MusicHandle, Params.Volume, Params.CrossFadeTime );
		return MusicHandle;
	}

	const FSoundHandle Old = MusicHandle;
	const FVoice* OldVoice = Resolve( Old );
	// 同じ音を Stream で重ねて鳴らすことはできないので、すぐ止める
	const bool bSameStream = Music->IsStream() && OldVoice != nullptr && OldVoice->Sound == Music;
	if ( OldVoice != nullptr ) Stop( Old, bSameStream ? 0.0f : Params.CrossFadeTime, EFadeCurve::EqualPower );

	FSoundPlayParams PlayParams;
	PlayParams.Bus = EAudioBus::Music;
	PlayParams.bLoop = Params.bLoop;
	PlayParams.Volume = Params.Volume;
	PlayParams.FadeInTime = Params.CrossFadeTime;
	PlayParams.FadeInCurve = EFadeCurve::EqualPower;
	PlayParams.Priority = 100;
	MusicHandle = Play( Music, PlayParams );
	MusicSound = Music;
	return MusicHandle;
}

void CAudioSystem::StopMusic( float32 FadeOut )
{
	Stop( MusicHandle, FadeOut, EFadeCurve::EqualPower );
	MusicHandle = FSoundHandle();
}

FSoundHandle CAudioSystem::PlayAmbient( const TSharedPtr<CSound>& Sound, const FAmbientParams& Params )
{
	FSoundPlayParams PlayParams;
	PlayParams.Bus = EAudioBus::Ambient;
	PlayParams.bLoop = true;
	PlayParams.Volume = Params.Volume;
	PlayParams.Pitch = Params.Pitch;
	PlayParams.FadeInTime = Params.FadeInTime;
	PlayParams.FadeInCurve = EFadeCurve::EqualPower;
	PlayParams.Priority = 10;
	PlayParams.bSpatial = Params.bSpatial;
	PlayParams.Position = Params.Position;
	PlayParams.MinDistance = Params.MinDistance;
	PlayParams.MaxDistance = Params.MaxDistance;
	PlayParams.Rolloff = Params.Rolloff;
	return Play( Sound, PlayParams );
}

//=============================================================================
// リスナー・更新
//=============================================================================
void CAudioSystem::SetListener( const FVector3& Position, const FVector3& Forward, const FVector3& Up )
{
	Listener.Position = Position;
	Listener.Forward = Forward;
	Listener.Up = Up;
}

void CAudioSystem::Apply( FVoice& Voice, bool bForce )
{
	IAudioBackend* B = GetBackend();
	if ( B == nullptr ) return;

	float32 SpatialGain = 1.0f;
	float32 SpatialPan = 0.0f;
	if ( Voice.bSpatial )
	{
		const FVector3 ToSource = Voice.Position - Listener.Position;
		SpatialGain = NAudio::RolloffGain( Voice.Rolloff, ToSource.Length(), Voice.MinDistance, Voice.MaxDistance );
		SpatialPan = NAudio::SpatialPan( ToSource, NAudio::ListenerRight( Listener ), Voice.PanStrength );
	}

	const float32 Volume = NMath::Max( Voice.Volume.Value * Voice.Gain.Value * GetBusGain( Voice.Bus ) * SpatialGain, 0.0f );
	const float32 Pan = NMath::Clamp( Voice.Pan.Value + SpatialPan, -1.0f, 1.0f );
	const float32 Pitch = NMath::Max( Voice.Pitch.Value, 0.01f );
	Voice.FinalVolume = Volume;
	Voice.FinalPan = Pan;

	if ( bForce || NMath::Abs( Volume - Voice.SentVolume ) > ChangeEpsilon )
	{
		B->SetVolume( Voice.BackendVoice, Volume );
		Voice.SentVolume = Volume;
	}
	if ( bForce || NMath::Abs( Pan - Voice.SentPan ) > ChangeEpsilon )
	{
		B->SetPan( Voice.BackendVoice, Pan );
		Voice.SentPan = Pan;
	}
	if ( bForce || NMath::Abs( Pitch - Voice.SentPitch ) > ChangeEpsilon )
	{
		B->SetPitch( Voice.BackendVoice, Pitch );
		Voice.SentPitch = Pitch;
	}
}

void CAudioSystem::Tick( float32 DeltaTime )
{
	Update( DeltaTime );
}

void CAudioSystem::Update( float32 DeltaSeconds )
{
	PROFILE_SCOPE_CAT( "Audio", "Update" );
	Clock += static_cast<float64>( DeltaSeconds );
	for ( FBus& Bus : Buses ) Bus.Volume.Step( DeltaSeconds );

	IAudioBackend* B = GetBackend();
	if ( B == nullptr ) return;

	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		FVoice& Voice = Voices[Index];
		if ( !Voice.bInUse ) continue;

		// 一時停止中の音は、フェードも止まる（再開すると続きから）
		if ( !Voice.bPaused )
		{
			Voice.Volume.Step( DeltaSeconds );
			Voice.Pitch.Step( DeltaSeconds );
			Voice.Pan.Step( DeltaSeconds );
			Voice.Gain.Step( DeltaSeconds );
		}

		if ( Voice.bStopWhenFaded && !Voice.Gain.IsFading() )
		{
			FreeVoice( Index );
			continue;
		}

		Apply( Voice, false );

		if ( !Voice.bPaused && !B->IsPlaying( Voice.BackendVoice ) ) FreeVoice( Index );   // 鳴り終わった
	}

	PROFILE_COUNTER( "Audio.Voices", NumActive );
	PROFILE_COUNTER_PERCENT( "Audio.VoiceUsage", 100.0 * static_cast<float64>( NumActive ) / static_cast<float64>( MaxVoices ) );

	// 古い再生時刻の記録を片付ける
	if ( LastPlayTime.Num() > 256 )
	{
		for ( auto It = LastPlayTime.CreateIterator(); It; ++It )
		{
			if ( Clock - It.Value() > 60.0 ) It.RemoveCurrent();
		}
	}
}

TArray<FVoiceInfo> CAudioSystem::GetVoiceInfos() const
{
	TArray<FVoiceInfo> Infos;
	for ( int32 Index = 0; Index < Voices.Num(); ++Index )
	{
		const FVoice& Voice = Voices[Index];
		if ( !Voice.bInUse ) continue;

		FVoiceInfo Info;
		Info.Handle = MakeHandle( Index );
		Info.SoundPath = Voice.Sound.IsValid() ? Voice.Sound->GetPath() : FString();
		Info.Bus = Voice.Bus;
		Info.Volume = Voice.FinalVolume;
		Info.Pan = Voice.FinalPan;
		Info.Pitch = Voice.Pitch.Value;
		Info.bLooping = Voice.bLoop;
		Info.bPaused = Voice.bPaused;
		Info.bFading = Voice.Gain.IsFading() || Voice.Volume.IsFading();
		Info.bSpatial = Voice.bSpatial;
		Info.Age = static_cast<float32>( Clock - Voice.StartTime );
		Infos.Add( Info );
	}
	return Infos;
}
