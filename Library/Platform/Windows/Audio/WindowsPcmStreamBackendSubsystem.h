// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/PcmStreamBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

struct IXAudio2SourceVoice;
class CWindowsAudioDeviceSubsystem;

//=============================================================================
// CWindowsPcmStreamBackendSubsystem
//   IPcmStreamBackend の、Windows（XAudio2）の実装。DxLib も描画 API も知らない。
//   ストリームごとに、XAudio2 のソースボイスを 1 つ持ち、Queue された波形（32 ビット浮動小数）を、順に積んで鳴らす。
//   積んだ波形は、鳴らし終わったぶんだけ、あとで（次の呼び出しのときに）捨てる。足りなくなると、無音になる。
//   Channels は 1〜2、SampleRate は 1000〜192000 Hz を作れる。メインスレッドだけから使う。
//=============================================================================
class CWindowsPcmStreamBackendSubsystem : public CAppSubsystem, public IPcmStreamBackend
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	// 音の出力を借りる（Initialize が呼ぶ。検査が、自分で作ったものを渡すのにも使う）
	void Bind( CWindowsAudioDeviceSubsystem* InDevice );
	void Deinitialize() override;

	int32 CreatePcmStream( int32 SampleRate, int32 Channels ) override;
	void DeletePcmStream( int32 Stream ) override;
	bool QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount ) override;
	int32 GetPcmStreamQueuedFrames( int32 Stream ) override;
	bool StartPcmStream( int32 Stream ) override;
	void StopPcmStream( int32 Stream ) override;
	void ResetPcmStream( int32 Stream ) override;
	bool IsPcmStreamPlaying( int32 Stream ) override;

private:
	struct FStream
	{
		int32 SampleRate = 0;
		int32 Channels = 0;
		IXAudio2SourceVoice* Source = nullptr;
		TArray<TArray<float32>> Buffers; // 積んだ順（先頭が、いちばん古い）
		TArray<int32> BufferFrames; // Buffers と同じ並び
		int64 QueuedFrames = 0; // まだ鳴らしていない波形のフレームの合計（鳴らしている途中のバッファーの、鳴らし終わったぶんを引いた値）
		uint64 SamplesBase = 0; // ソースボイスの再生済みサンプル数の、数え始め（キューが空になるたびに合わせる）
		int64 RemovedFrames = 0; // 数え始めから、鳴らし終わって捨てた波形のフレーム数
		bool bPlaying = false;
	};

	bool CreateSource( FStream& Stream );
	void DestroySource( FStream& Stream );
	// 鳴らし終わった波形を捨てて、残りのフレーム数を合わせる
	void Collect( FStream& Stream );

	CWindowsAudioDeviceSubsystem* Device = nullptr;
	TMap<int32, FStream> Streams;
	int32 NextHandle = 1;
};
