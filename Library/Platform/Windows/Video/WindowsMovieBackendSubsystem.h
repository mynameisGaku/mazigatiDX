// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Asset/TextureBackend.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Interfaces/Video/MovieBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Platform/System/WindowsInclude.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

struct IXAudio2SourceVoice;
class CWindowsAudioDeviceSubsystem;

//=============================================================================
// CWindowsMovieBackendSubsystem
//   IMovieBackend の、Windows（Media Foundation + XAudio2）の実装。描画 API も DxLib も知らない。
//   ムービーごとに、Media Foundation の読み出し（IMFSourceReader。H.264 の mp4 / wmv / avi など、OS が読める形式）で、映像を 32 ビットの画素へ、
//   音声を浮動小数の PCM へ読み出す。映像は、画像（ITextureBackend の CreateTextureFromPixels で作り、UpdateTexturePixels で更新する）になる。
//   音声は、XAudio2 のソースボイスへ先読みして積む。
//
//   - 時計は、Tick の DeltaTime を、再生の速さ（SetMovieSpeed）を掛けて足していく（止めれば止まる）。映像は、時計に合うコマまで進めて、画像を更新する
//   - 1 回の Tick で読むコマは限る（遅れても、固まらない。追いつくまで、数回の Tick に分けて進める）
//   - 音声の機器が無い、またはムービーに音声が無ければ、映像だけ
//   - メインスレッドだけから使う
//=============================================================================
class CWindowsMovieBackendSubsystem : public CAppSubsystem, public IMovieBackend, public ITickable
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	// 再生中のムービーの時計を進め、映像と音声を読み進める
	void Tick( float32 DeltaTime ) override;
	// 画像の作り先と、音声の出力を借りる（Initialize が呼ぶ。検査が、自分で作ったものを渡すのにも使う。音声の出力は無くてもよい）
	void Bind( ITextureBackend* InTextures, CWindowsAudioDeviceSubsystem* InAudio );

	int32 OpenMovie( const FString& Path ) override;
	void CloseMovie( int32 Movie ) override;
	int32 GetMovieImage( int32 Movie ) override;
	bool GetMovieInfo( int32 Movie, FMovieInfo& OutInfo ) override;
	bool PlayMovie( int32 Movie, bool bLoop ) override;
	void PauseMovie( int32 Movie ) override;
	bool IsMoviePlaying( int32 Movie ) override;
	float32 GetMoviePosition( int32 Movie ) override;
	void SeekMovie( int32 Movie, float32 Seconds ) override;
	void SetMovieVolume( int32 Movie, float32 Volume ) override;
	bool SetMovieSpeed( int32 Movie, float32 Speed ) override;

private:
	struct FFrame
	{
		TArray<FColor32> Pixels;
		double Time = 0.0; // 秒
		bool bValid = false;
	};

	struct FMovie
	{
		Microsoft::WRL::ComPtr<IMFSourceReader> Reader;
		FMovieInfo Info;
		bool bHasVideo = false;
		bool bHasAudio = false;
		bool bBottomUp = false; // 映像の行が、下から並んでいる
		int32 Image = -1;
		// 時計
		bool bPlaying = false;
		bool bLoop = false;
		float32 Speed = 1.0f;
		float32 Volume = 1.0f;
		double Position = 0.0;
		// 映像
		FFrame Next; // 読んであるが、まだ見せない先のコマ
		bool bVideoEnded = false;
		bool bImageDirty = false;
		TArray<FColor32> Current;
		// 音声
		IXAudio2SourceVoice* Voice = nullptr;
		int32 AudioSampleRate = 0;
		int32 AudioChannels = 0;
		TArray<TArray<float32>> AudioBuffers; // 積んだ順
		TArray<double> AudioBufferSeconds;
		bool bAudioEnded = false;
		bool bVoiceStarted = false;
	};

	bool OpenReader( const FString& Path, FMovie& Movie );
	bool ReadVideoFrame( FMovie& Movie, FFrame& OutFrame );
	void FeedAudio( FMovie& Movie );
	void AdvanceVideo( FMovie& Movie );
	void SeekInternal( FMovie& Movie, double Seconds );
	void FinishOrLoop( FMovie& Movie );
	void ApplyAudioMix( FMovie& Movie );
	void DestroyVoice( FMovie& Movie );

	ITextureBackend* Textures = nullptr;
	CWindowsAudioDeviceSubsystem* Audio = nullptr;
	TMap<int32, FMovie> Movies;
	int32 NextHandle = 1;
	bool bMediaStarted = false;
	bool bComInitialized = false;
};
