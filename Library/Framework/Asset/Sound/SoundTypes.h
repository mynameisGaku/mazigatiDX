// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// サウンドの値の型（バックエンドに依らない）
//=============================================================================

// 読み込んだ音の情報。分からない項目は 0
struct FSoundInfo
{
	// 長さ（秒）
	float32 LengthSeconds = 0.0f;
	// 1 チャンネルあたりのサンプルの数（フレーム数）
	int64 FrameCount = 0;
	// 1 秒あたりのサンプルの数（Hz）
	int32 SampleRate = 0;
	// チャンネル数（1 = モノラル、2 = ステレオ）
	int32 Channels = 0;
};

// 生の音のデータ（PCM）。チャンネルを交互に並べた、-1..1 の浮動小数。
// 音を計算で作る・読み込んだ音を加工する・WAV に保存する、といったときに使う
struct FPcmBuffer
{
	int32 SampleRate = 44100;
	int32 Channels = 1;
	// チャンネルを交互に並べたサンプル（L R L R ... / M M M ...）。範囲は -1..1（超えた分は、使うときに切る）
	TArray<float32> Samples;

	// 1 チャンネルあたりのサンプルの数
	int64 GetFrameCount() const
	{
		return Channels > 0 ? static_cast<int64>( Samples.Num() ) / Channels : 0;
	}

	// 長さ（秒）
	float32 GetLengthSeconds() const
	{
		return SampleRate > 0 ? static_cast<float32>( GetFrameCount() ) / static_cast<float32>( SampleRate ) : 0.0f;
	}

	// ループ区間（フレーム。LoopStartFrame を含み、LoopEndFrame を含まない）。負なら、ループ区間なし。
	// WAV の smpl チャンク（他のツールとも共通の、ループの印）として、保存・読み込みされる。加工（NPcmEdit）は、区間が無事な範囲で、位置を保つ
	int64 LoopStartFrame = -1;
	int64 LoopEndFrame = -1;

	// 音の中に収まる、空でないループ区間があるか
	bool HasLoop() const
	{
		return LoopStartFrame >= 0 && LoopEndFrame > LoopStartFrame && LoopEndFrame <= GetFrameCount();
	}

	void ClearLoop()
	{
		LoopStartFrame = -1;
		LoopEndFrame = -1;
	}

	// 音として使える形か（サンプルがあり、チャンネル数で割り切れ、1〜8 チャンネル、1 Hz 以上）
	bool IsValid() const
	{
		return SampleRate > 0 && Channels >= 1 && Channels <= 8 && !Samples.IsEmpty() && Samples.Num() % Channels == 0;
	}
};
