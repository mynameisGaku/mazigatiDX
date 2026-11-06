// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/PcmStreamBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullPcmStreamBackendSubsystem
//   IPcmStreamBackend の、DxLib を使わない実装。音は出さず、ストリームごとの状態（足した波形の量・再生中か）を覚えるだけ。
//   --backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   - 時間は進まない: 足した波形は、Consume を呼ぶまで減らない
//   - 作れる形は、サンプルレートが 1 以上、チャンネルが 1〜8
//=============================================================================
class CNullPcmStreamBackendSubsystem : public CAppSubsystem, public IPcmStreamBackend
{
public:
	// ストリーム 1 つの状態
	struct FStreamState
	{
		int32 SampleRate = 0;
		int32 Channels = 0;
		bool bPlaying = false;
		// 足した波形の総フレーム数と、鳴らし終わった（Consume した）フレーム数
		int64 TotalQueuedFrames = 0;
		int64 ConsumedFrames = 0;
		// 最後に足した波形の、最初のサンプル（確認用）
		float32 LastFirstSample = 0.0f;
	};

	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	int32 CreatePcmStream( int32 SampleRate, int32 Channels ) override;
	void DeletePcmStream( int32 Stream ) override;
	bool QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount ) override;
	int32 GetPcmStreamQueuedFrames( int32 Stream ) override;
	bool StartPcmStream( int32 Stream ) override;
	void StopPcmStream( int32 Stream ) override;
	void ResetPcmStream( int32 Stream ) override;
	bool IsPcmStreamPlaying( int32 Stream ) override;

	//-------------------------------------------------------------------------
	// Null 固有（確認用）
	//-------------------------------------------------------------------------
	// 再生中のストリームが、Frames だけ鳴らし終わったことにする（足してある分を超えない）。鳴らし終わったフレーム数を返す
	int32 Consume( int32 Stream, int32 Frames );
	// ストリームの状態。無ければ初めの状態
	FStreamState GetStreamState( int32 Stream ) const;
	int32 GetNumStreams() const;

private:
	TMap<int32, FStreamState> Streams;
	int32 NextHandle = 0;
};
