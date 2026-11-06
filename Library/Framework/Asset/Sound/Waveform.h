// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

//=============================================================================
// CWaveformPeaks
//   波形の表示に使う「山谷」（一定の幅ごとの、最小と最大）。長い音を、画面の幅に合わせて縮めるときに、全サンプルを数えずに済むように、
//   64 フレームごとの山谷を作り、それを 2 つずつ束ねた階層を持つ（Build は 1 回。音を変えたら、作り直す）。
//   GetColumns は、見ている範囲を列に分け、列ごとの最小・最大を返す。結果は、全サンプルを数えた結果と同じ（階層は、列の端でない所だけに使い、端は、元のサンプルを数える）。
//
//   CWaveformPeaks Peaks;  Peaks.Build( Pcm );
//   TArray<FWaveformPeak> Columns;
//   Peaks.GetColumns( Pcm, ViewStartFrame, ViewEndFrame, 800, -1, Columns );   // 800 列。-1 = 全チャンネルを合わせる
//   // Columns[i].Min .. Max を、i 列目の縦の線として描く
//
//   - 数でない値（NaN・無限大）は、0 として扱う
//   - Build に使った Pcm を、GetColumns にも渡す（端の列を正確に求めるため。変えていないこと）
//=============================================================================
struct FWaveformPeak
{
	float32 Min = 0.0f;
	float32 Max = 0.0f;
};

class CWaveformPeaks
{
public:
	// 最初の階層の 1 つ分のフレーム数
	static constexpr int32 BaseBucketFrames = 64;

	// Pcm の山谷の階層を作る（以前のものは捨てる）。音として使えなければ、空のまま
	void Build( const FPcmBuffer& Pcm );
	void Reset();

	bool IsBuilt() const { return bBuilt; }
	int64 GetFrameCount() const { return FrameCount; }
	int32 GetChannelCount() const { return Channels; }
	int32 GetNumLevels() const { return Levels.Num(); }

	// [Start, End) を NumColumns 個の列に等分して、各列の最小・最大を Out に入れる（Out は列の数に合わせる）。
	// Channel が 0 以上ならそのチャンネルだけ、負なら全チャンネルを合わせる。音の外の列は 0。
	// 列が 1 フレームより細かいとき（NumColumns が範囲のフレーム数より多い）は、列に最も近い 1 フレームの値を返す
	void GetColumns( const FPcmBuffer& Pcm, int64 Start, int64 End, int32 NumColumns, int32 Channel, TArray<FWaveformPeak>& Out ) const;

	// 1 つの範囲 [Start, End) の最小・最大（音の長さに切り詰める。空なら 0）
	FWaveformPeak GetRange( const FPcmBuffer& Pcm, int64 Start, int64 End, int32 Channel ) const;

private:
	bool bBuilt = false;
	int64 FrameCount = 0;
	int32 Channels = 0;
	// Levels[k] は、BaseBucketFrames * 2^k フレームごとの山谷。添字は、バケット * Channels + チャンネル。最後のバケットは、端数でもよい
	TArray<TArray<FWaveformPeak>> Levels;
};
