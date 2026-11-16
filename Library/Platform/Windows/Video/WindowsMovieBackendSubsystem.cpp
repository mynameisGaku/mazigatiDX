// SPDX-License-Identifier: MIT
#include "WindowsMovieBackendSubsystem.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/Windows/Audio/WindowsAudioDeviceSubsystem.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <xaudio2.h>

#pragma comment( lib, "mfplat.lib" )
#pragma comment( lib, "mfreadwrite.lib" )
#pragma comment( lib, "mfuuid.lib" )
#pragma comment( lib, "propsys.lib" )

REGISTER_SUBSYSTEM( CWindowsMovieBackendSubsystem )

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr double HundredNanoseconds = 10000000.0;
	constexpr int32 MaxFramesPerTick = 8; // 1 回の Tick で、映像を読み進める上限（追いつくまで、数回に分ける）
	constexpr double AudioLookAheadSeconds = 0.5; // 音声を、これだけ先まで積んでおく
}

bool CWindowsMovieBackendSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsMovieBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	IAssetBackend* Assets = Collection.InitializeDependency<IAssetBackend>();
	Bind( Assets, Collection.InitializeDependency<CWindowsAudioDeviceSubsystem>() );
}

void CWindowsMovieBackendSubsystem::Bind( ITextureBackend* InTextures, CWindowsAudioDeviceSubsystem* InAudio )
{
	Textures = InTextures;
	Audio = InAudio;
	if ( !bMediaStarted )
	{
		bComInitialized = SUCCEEDED( CoInitializeEx( nullptr, COINIT_MULTITHREADED ) );
		bMediaStarted = SUCCEEDED( MFStartup( MF_VERSION, MFSTARTUP_FULL ) );
	}
}

void CWindowsMovieBackendSubsystem::Deinitialize()
{
	TArray<int32> Handles;
	for ( auto& Pair : Movies )
	{
		Handles.Add( Pair.Key );
	}
	for ( int32 Handle : Handles )
	{
		CloseMovie( Handle );
	}
	Movies.Reset();
	Textures = nullptr;
	Audio = nullptr;
	if ( bMediaStarted )
	{
		MFShutdown();
		bMediaStarted = false;
	}
	if ( bComInitialized )
	{
		CoUninitialize();
		bComInitialized = false;
	}
}

bool CWindowsMovieBackendSubsystem::OpenReader( const FString& Path, FMovie& Movie )
{
	ComPtr<IMFAttributes> Attributes;
	if ( FAILED( MFCreateAttributes( &Attributes, 1 ) ) )
	{
		return false;
	}
	Attributes->SetUINT32( MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE ); // 映像を、RGB に変換してもらう
	if ( FAILED( MFCreateSourceReaderFromURL( Path.ToWide().GetData(), Attributes.Get(), &Movie.Reader ) ) )
	{
		return false;
	}
	Movie.Reader->SetStreamSelection( MF_SOURCE_READER_ALL_STREAMS, FALSE );

	// 映像
	ComPtr<IMFMediaType> VideoType;
	if ( SUCCEEDED( MFCreateMediaType( &VideoType ) ) )
	{
		VideoType->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Video );
		VideoType->SetGUID( MF_MT_SUBTYPE, MFVideoFormat_RGB32 );
		if ( SUCCEEDED( Movie.Reader->SetCurrentMediaType( MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, VideoType.Get() ) ) )
		{
			ComPtr<IMFMediaType> Actual;
			UINT32 Width = 0;
			UINT32 Height = 0;
			if ( SUCCEEDED( Movie.Reader->GetCurrentMediaType( MF_SOURCE_READER_FIRST_VIDEO_STREAM, &Actual ) ) && SUCCEEDED( MFGetAttributeSize( Actual.Get(), MF_MT_FRAME_SIZE, &Width, &Height ) ) && Width > 0 && Height > 0 && Width <= 16384 && Height <= 16384 )
			{
				Movie.bHasVideo = true;
				Movie.Info.Width = static_cast<int32>( Width );
				Movie.Info.Height = static_cast<int32>( Height );
				UINT32 Numerator = 0;
				UINT32 Denominator = 0;
				if ( SUCCEEDED( MFGetAttributeRatio( Actual.Get(), MF_MT_FRAME_RATE, &Numerator, &Denominator ) ) && Denominator > 0 )
				{
					Movie.Info.FrameRate = static_cast<float32>( Numerator ) / static_cast<float32>( Denominator );
				}
				UINT32 Stride = 0;
				if ( SUCCEEDED( Actual->GetUINT32( MF_MT_DEFAULT_STRIDE, &Stride ) ) )
				{
					Movie.bBottomUp = static_cast<INT32>( Stride ) < 0;
				}
				Movie.Reader->SetStreamSelection( MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE );
			}
		}
	}
	// 音声（ステレオまで）
	ComPtr<IMFMediaType> AudioType;
	if ( SUCCEEDED( MFCreateMediaType( &AudioType ) ) )
	{
		AudioType->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
		AudioType->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_Float );
		AudioType->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 32 );
		ComPtr<IMFMediaType> Native;
		UINT32 NativeChannels = 0;
		if ( SUCCEEDED( Movie.Reader->GetNativeMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &Native ) ) )
		{
			Native->GetUINT32( MF_MT_AUDIO_NUM_CHANNELS, &NativeChannels );
			if ( NativeChannels > 2 )
			{
				AudioType->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, 2 );
			}
			if ( SUCCEEDED( Movie.Reader->SetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, AudioType.Get() ) ) )
			{
				ComPtr<IMFMediaType> Actual;
				UINT32 Channels = 0;
				UINT32 Rate = 0;
				if ( SUCCEEDED( Movie.Reader->GetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, &Actual ) ) && SUCCEEDED( Actual->GetUINT32( MF_MT_AUDIO_NUM_CHANNELS, &Channels ) ) && SUCCEEDED( Actual->GetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, &Rate ) ) && Channels >= 1 && Channels <= 2 && Rate > 0 )
				{
					Movie.bHasAudio = true;
					Movie.AudioChannels = static_cast<int32>( Channels );
					Movie.AudioSampleRate = static_cast<int32>( Rate );
					Movie.Reader->SetStreamSelection( MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE );
				}
			}
		}
	}
	if ( !Movie.bHasVideo && !Movie.bHasAudio )
	{
		return false;
	}
	PROPVARIANT Duration;
	PropVariantInit( &Duration );
	if ( SUCCEEDED( Movie.Reader->GetPresentationAttribute( MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &Duration ) ) && Duration.vt == VT_UI8 )
	{
		Movie.Info.DurationSeconds = static_cast<float32>( static_cast<double>( Duration.uhVal.QuadPart ) / HundredNanoseconds );
	}
	PropVariantClear( &Duration );
	return true;
}

int32 CWindowsMovieBackendSubsystem::OpenMovie( const FString& Path )
{
	if ( !bMediaStarted || Textures == nullptr || Path.IsEmpty() )
	{
		return -1;
	}
	FMovie Movie;
	if ( !OpenReader( Path, Movie ) )
	{
		return -1;
	}
	// 映像の画像（最初は黒）。音声だけのムービーは、1 画素の黒
	const int32 Width = Movie.bHasVideo ? Movie.Info.Width : 1;
	const int32 Height = Movie.bHasVideo ? Movie.Info.Height : 1;
	TArray<FColor32> Black;
	Black.SetNum( Width * Height, FColor32( 0, 0, 0, 255 ) );
	Movie.Image = Textures->CreateTextureFromPixels( Width, Height, Black.GetData() );
	if ( Movie.Image < 0 )
	{
		return -1;
	}
	Movie.Current = Black;
	if ( Movie.bHasAudio && Audio != nullptr && Audio->IsAvailable() )
	{
		WAVEFORMATEX Format = {};
		Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		Format.nChannels = static_cast<WORD>( Movie.AudioChannels );
		Format.nSamplesPerSec = static_cast<DWORD>( Movie.AudioSampleRate );
		Format.wBitsPerSample = 32;
		Format.nBlockAlign = static_cast<WORD>( Format.nChannels * sizeof( float32 ) );
		Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
		if ( FAILED( Audio->GetEngine()->CreateSourceVoice( &Movie.Voice, &Format, 0, 16.0f ) ) )
		{
			Movie.Voice = nullptr; // 音声なしで、映像だけ
		}
	}
	const int32 Handle = NextHandle++;
	FMovie& Stored = Movies.FindOrAdd( Handle );
	Stored = MoveTemp( Movie );
	ApplyAudioMix( Stored );
	return Handle;
}

void CWindowsMovieBackendSubsystem::DestroyVoice( FMovie& Movie )
{
	if ( Movie.Voice != nullptr )
	{
		Movie.Voice->Stop();
		Movie.Voice->DestroyVoice();
		Movie.Voice = nullptr;
	}
}

void CWindowsMovieBackendSubsystem::CloseMovie( int32 Movie )
{
	FMovie* Found = Movies.Find( Movie );
	if ( Found == nullptr )
	{
		return;
	}
	DestroyVoice( *Found );
	if ( Textures != nullptr && Found->Image >= 0 )
	{
		Textures->DeleteTexture( Found->Image );
	}
	Found->Reader.Reset();
	Movies.Remove( Movie );
}

int32 CWindowsMovieBackendSubsystem::GetMovieImage( int32 Movie )
{
	const FMovie* Found = Movies.Find( Movie );
	return Found != nullptr ? Found->Image : -1;
}

bool CWindowsMovieBackendSubsystem::GetMovieInfo( int32 Movie, FMovieInfo& OutInfo )
{
	const FMovie* Found = Movies.Find( Movie );
	if ( Found == nullptr )
	{
		return false;
	}
	OutInfo = Found->Info;
	return true;
}

void CWindowsMovieBackendSubsystem::ApplyAudioMix( FMovie& Movie )
{
	if ( Movie.Voice != nullptr )
	{
		Movie.Voice->SetVolume( NMath::Max( 0.0f, Movie.Volume ) );
		Movie.Voice->SetFrequencyRatio( NMath::Clamp( Movie.Speed, 1.0f / 1024.0f, 16.0f ) );
	}
}

bool CWindowsMovieBackendSubsystem::ReadVideoFrame( FMovie& Movie, FFrame& OutFrame )
{
	OutFrame.bValid = false;
	for ( int32 Attempt = 0; Attempt < 64; ++Attempt ) // 映像のコマが無い tick（隙間）は、飛ばして読む
	{
		DWORD Flags = 0;
		LONGLONG Time = 0;
		ComPtr<IMFSample> Sample;
		if ( FAILED( Movie.Reader->ReadSample( MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &Flags, &Time, &Sample ) ) )
		{
			Movie.bVideoEnded = true;
			return false;
		}
		if ( Flags & ( MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR ) )
		{
			Movie.bVideoEnded = true;
			return false;
		}
		if ( !Sample )
		{
			continue;
		}
		ComPtr<IMFMediaBuffer> Buffer;
		BYTE* Data = nullptr;
		DWORD Length = 0;
		if ( FAILED( Sample->ConvertToContiguousBuffer( &Buffer ) ) || FAILED( Buffer->Lock( &Data, nullptr, &Length ) ) )
		{
			continue;
		}
		const int32 Width = Movie.Info.Width;
		const int32 Height = Movie.Info.Height;
		if ( Length >= static_cast<DWORD>( Width * Height * 4 ) )
		{
			OutFrame.Pixels.SetNum( Width * Height, FColor32() );
			for ( int32 Row = 0; Row < Height; ++Row )
			{
				const BYTE* Source = Data + static_cast<size_t>( Movie.bBottomUp ? Height - 1 - Row : Row ) * Width * 4;
				FColor32* Dest = OutFrame.Pixels.GetData() + static_cast<size_t>( Row ) * Width;
				for ( int32 Column = 0; Column < Width; ++Column )
				{
					Dest[Column] = FColor32( Source[Column * 4 + 2], Source[Column * 4 + 1], Source[Column * 4 + 0], 255 ); // BGRX -> RGBA
				}
			}
			OutFrame.Time = static_cast<double>( Time ) / HundredNanoseconds;
			OutFrame.bValid = true;
		}
		Buffer->Unlock();
		if ( OutFrame.bValid )
		{
			return true;
		}
	}
	return false;
}

void CWindowsMovieBackendSubsystem::AdvanceVideo( FMovie& Movie )
{
	if ( !Movie.bHasVideo )
	{
		return;
	}
	if ( !Movie.Next.bValid && !Movie.bVideoEnded )
	{
		ReadVideoFrame( Movie, Movie.Next );
	}
	// 時計に合うコマまで進める。いちばん新しい、時刻が時計以下のコマを見せる
	for ( int32 Count = 0; Count < MaxFramesPerTick && Movie.Next.bValid && Movie.Next.Time <= Movie.Position; ++Count )
	{
		Movie.Current = MoveTemp( Movie.Next.Pixels );
		Movie.bImageDirty = true;
		Movie.Next.bValid = false;
		if ( !Movie.bVideoEnded )
		{
			ReadVideoFrame( Movie, Movie.Next );
		}
	}
	if ( Movie.bImageDirty && Textures != nullptr )
	{
		Textures->UpdateTexturePixels( Movie.Image, Movie.Info.Width, Movie.Info.Height, Movie.Current.GetData() );
		Movie.bImageDirty = false;
	}
}

void CWindowsMovieBackendSubsystem::FeedAudio( FMovie& Movie )
{
	if ( Movie.Voice == nullptr )
	{
		return;
	}
	XAUDIO2_VOICE_STATE State = {};
	Movie.Voice->GetState( &State, XAUDIO2_VOICE_NOSAMPLESPLAYED );
	while ( static_cast<UINT32>( Movie.AudioBuffers.Num() ) > State.BuffersQueued ) // 鳴らし終わったバッファーを捨てる
	{
		Movie.AudioBuffers.RemoveAt( 0 );
		Movie.AudioBufferSeconds.RemoveAt( 0 );
	}
	double Queued = 0.0;
	for ( double Seconds : Movie.AudioBufferSeconds )
	{
		Queued += Seconds;
	}
	for ( int32 Guard = 0; Guard < 64 && !Movie.bAudioEnded && Queued < AudioLookAheadSeconds; ++Guard )
	{
		DWORD Flags = 0;
		ComPtr<IMFSample> Sample;
		if ( FAILED( Movie.Reader->ReadSample( MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &Flags, nullptr, &Sample ) ) || ( Flags & ( MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR ) ) )
		{
			Movie.bAudioEnded = true;
			break;
		}
		if ( !Sample )
		{
			continue;
		}
		ComPtr<IMFMediaBuffer> Buffer;
		BYTE* Data = nullptr;
		DWORD Length = 0;
		if ( FAILED( Sample->ConvertToContiguousBuffer( &Buffer ) ) || FAILED( Buffer->Lock( &Data, nullptr, &Length ) ) )
		{
			continue;
		}
		const int32 Count = static_cast<int32>( Length / sizeof( float32 ) );
		TArray<float32> Samples;
		Samples.SetNum( Count, 0.0f );
		const float32* Floats = reinterpret_cast<const float32*>( Data );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Samples[Index] = Floats[Index];
		}
		Buffer->Unlock();
		if ( Count < Movie.AudioChannels )
		{
			continue;
		}
		const double Seconds = static_cast<double>( Count / Movie.AudioChannels ) / static_cast<double>( Movie.AudioSampleRate );
		Movie.AudioBuffers.Add( MoveTemp( Samples ) );
		Movie.AudioBufferSeconds.Add( Seconds );
		TArray<float32>& Stored = Movie.AudioBuffers[Movie.AudioBuffers.Num() - 1];
		XAUDIO2_BUFFER Submit = {};
		Submit.AudioBytes = static_cast<UINT32>( Stored.Num() * sizeof( float32 ) );
		Submit.pAudioData = reinterpret_cast<const BYTE*>( Stored.GetData() );
		if ( FAILED( Movie.Voice->SubmitSourceBuffer( &Submit ) ) )
		{
			Movie.AudioBuffers.RemoveAt( Movie.AudioBuffers.Num() - 1 );
			Movie.AudioBufferSeconds.RemoveAt( Movie.AudioBufferSeconds.Num() - 1 );
			break;
		}
		Queued += Seconds;
	}
}

void CWindowsMovieBackendSubsystem::SeekInternal( FMovie& Movie, double Seconds )
{
	Seconds = Seconds < 0.0 ? 0.0 : Seconds;
	if ( Movie.Info.DurationSeconds > 0.0f && Seconds > static_cast<double>( Movie.Info.DurationSeconds ) )
	{
		Seconds = static_cast<double>( Movie.Info.DurationSeconds );
	}
	PROPVARIANT Target;
	PropVariantInit( &Target );
	Target.vt = VT_I8;
	Target.hVal.QuadPart = static_cast<LONGLONG>( Seconds * HundredNanoseconds );
	Movie.Reader->SetCurrentPosition( GUID_NULL, Target );
	PropVariantClear( &Target );
	if ( Movie.Voice != nullptr )
	{
		Movie.Voice->Stop();
		Movie.Voice->FlushSourceBuffers();
		Movie.bVoiceStarted = false;
	}
	Movie.AudioBuffers.Reset();
	Movie.AudioBufferSeconds.Reset();
	Movie.Next.bValid = false;
	Movie.bVideoEnded = false;
	Movie.bAudioEnded = false;
	Movie.Position = Seconds;
	// 位置のコマを、すぐ見せる（止めたままのシークでも、画像が変わる）。シーク先の手前のキーフレームから読み直されるので、位置に追いつくまで読む
	for ( int32 Guard = 0; Guard < 600; ++Guard )
	{
		AdvanceVideo( Movie );
		if ( !Movie.bHasVideo || !Movie.Next.bValid || Movie.Next.Time > Movie.Position )
		{
			break;
		}
	}
}

void CWindowsMovieBackendSubsystem::FinishOrLoop( FMovie& Movie )
{
	if ( Movie.bLoop )
	{
		SeekInternal( Movie, 0.0 );
		return;
	}
	Movie.bPlaying = false;
	if ( Movie.Info.DurationSeconds > 0.0f )
	{
		Movie.Position = static_cast<double>( Movie.Info.DurationSeconds );
	}
	if ( Movie.Voice != nullptr )
	{
		Movie.Voice->Stop();
		Movie.bVoiceStarted = false;
	}
}

void CWindowsMovieBackendSubsystem::Tick( float32 DeltaTime )
{
	for ( auto& Pair : Movies )
	{
		FMovie& Movie = Pair.Value;
		if ( !Movie.bPlaying )
		{
			continue;
		}
		Movie.Position += static_cast<double>( DeltaTime ) * static_cast<double>( Movie.Speed );
		AdvanceVideo( Movie );
		FeedAudio( Movie );
		if ( Movie.Voice != nullptr && !Movie.bVoiceStarted && !Movie.AudioBuffers.IsEmpty() )
		{
			Movie.Voice->Start();
			Movie.bVoiceStarted = true;
		}
		// 終わり: 映像と音声を読み切り、音声も鳴らし切った
		bool bAudioDrained = true;
		if ( Movie.Voice != nullptr )
		{
			XAUDIO2_VOICE_STATE State = {};
			Movie.Voice->GetState( &State, XAUDIO2_VOICE_NOSAMPLESPLAYED );
			bAudioDrained = Movie.bAudioEnded && State.BuffersQueued == 0;
		}
		const bool bVideoDone = !Movie.bHasVideo || ( Movie.bVideoEnded && !Movie.Next.bValid );
		const bool bAudioDone = !Movie.bHasAudio || Movie.Voice == nullptr || bAudioDrained;
		const bool bPastEnd = Movie.Info.DurationSeconds > 0.0f && Movie.Position >= static_cast<double>( Movie.Info.DurationSeconds ) + 0.05;
		if ( ( bVideoDone && bAudioDone ) || ( bPastEnd && bVideoDone ) )
		{
			FinishOrLoop( Movie );
		}
	}
}

bool CWindowsMovieBackendSubsystem::PlayMovie( int32 Movie, bool bLoop )
{
	FMovie* Found = Movies.Find( Movie );
	if ( Found == nullptr )
	{
		return false;
	}
	Found->bLoop = bLoop;
	if ( Found->Info.DurationSeconds > 0.0f && Found->Position >= static_cast<double>( Found->Info.DurationSeconds ) )
	{
		SeekInternal( *Found, 0.0 ); // 終わりまで来て止まっていたものは、頭から
	}
	Found->bPlaying = true;
	if ( Found->Voice != nullptr && Found->bVoiceStarted == false && !Found->AudioBuffers.IsEmpty() )
	{
		Found->Voice->Start();
		Found->bVoiceStarted = true;
	}
	return true;
}

void CWindowsMovieBackendSubsystem::PauseMovie( int32 Movie )
{
	FMovie* Found = Movies.Find( Movie );
	if ( Found == nullptr )
	{
		return;
	}
	Found->bPlaying = false;
	if ( Found->Voice != nullptr )
	{
		Found->Voice->Stop();
		Found->bVoiceStarted = false;
	}
}

bool CWindowsMovieBackendSubsystem::IsMoviePlaying( int32 Movie )
{
	const FMovie* Found = Movies.Find( Movie );
	return Found != nullptr && Found->bPlaying;
}

float32 CWindowsMovieBackendSubsystem::GetMoviePosition( int32 Movie )
{
	const FMovie* Found = Movies.Find( Movie );
	return Found != nullptr ? static_cast<float32>( Found->Position ) : 0.0f;
}

void CWindowsMovieBackendSubsystem::SeekMovie( int32 Movie, float32 Seconds )
{
	if ( FMovie* Found = Movies.Find( Movie ) )
	{
		SeekInternal( *Found, static_cast<double>( Seconds ) );
	}
}

void CWindowsMovieBackendSubsystem::SetMovieVolume( int32 Movie, float32 Volume )
{
	if ( FMovie* Found = Movies.Find( Movie ) )
	{
		Found->Volume = Volume;
		ApplyAudioMix( *Found );
	}
}

bool CWindowsMovieBackendSubsystem::SetMovieSpeed( int32 Movie, float32 Speed )
{
	FMovie* Found = Movies.Find( Movie );
	if ( Found == nullptr || !( Speed > 0.0f ) || Speed > 16.0f )
	{
		return false;
	}
	Found->Speed = Speed;
	ApplyAudioMix( *Found );
	return true;
}
