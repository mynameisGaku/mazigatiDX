// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IPcmStreamBackend
//   ゲームが実行時に作った波形（PCM）を、順に流して鳴らす窓口（手続き的な音・シンセ・独自のデコーダーの出力など）。
//   ファイルや PCM の塊から作る音（IAssetBackend / IAudioBackend）とは別。使う側は CPcmStream。
//   実装: CDxLibPcmStreamBackendSubsystem（DxLib）、CNullPcmStreamBackendSubsystem（鳴らさない確認用）
//
//   - 流れ: 作る -> Queue で波形を足す -> Start で鳴らす。鳴らした分だけ、足した波形は減っていく。足りなくなると無音になる
//   - 波形は、チャンネルを交互に並べた -1..1 の浮動小数（範囲を超えた分は切る）
//   - 音量・パン・ピッチは窓口に無い。波形を作る側で決める（流す前に掛ける）
//   - ストリームは int32 のハンドルで表す。負の値（無効）は使えない
//   - すべての関数はメインスレッドだけから呼ばれる
//=============================================================================
class IPcmStreamBackend
{
public:
	virtual ~IPcmStreamBackend() = default;

	// 作る。SampleRate は 1 秒あたりのフレーム数（Hz）、Channels は 1〜8。作れなければ負（対応しない Channels や SampleRate は作れない）
	virtual int32 CreatePcmStream( int32 SampleRate, int32 Channels ) = 0;
	virtual void DeletePcmStream( int32 Stream ) = 0;

	// 波形を末尾に足す。FrameCount は 1 チャンネルあたりのフレーム数（Samples は FrameCount * Channels 個）。足せなければ false
	virtual bool QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount ) = 0;
	// まだ鳴らしていない波形のフレーム数。足した分から、鳴らし終わった分を引いたもの
	virtual int32 GetPcmStreamQueuedFrames( int32 Stream ) = 0;

	// 鳴らし始める（足してある波形から）。始められなければ false
	virtual bool StartPcmStream( int32 Stream ) = 0;
	// 止める。足してある波形は残るので、Start で続きから鳴らせる
	virtual void StopPcmStream( int32 Stream ) = 0;
	// 足してある波形を捨てて、はじめの状態（止まっていて、波形が空）に戻す
	virtual void ResetPcmStream( int32 Stream ) = 0;
	virtual bool IsPcmStreamPlaying( int32 Stream ) = 0;
};
