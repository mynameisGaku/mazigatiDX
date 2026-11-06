// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

//=============================================================================
// NPcmEdit
//   生の音（FPcmBuffer）を、切り出す・音量をそろえる・フェードさせる・割れた所を見つける、状態を持たない関数（サウンドエディターの部品。鳴らさずに検査できる）。
//   範囲は「フレーム」（1 チャンネルあたりのサンプルの位置）で、[Start, End)（End は含まない）。範囲は、音の長さに切り詰める。
//   数でない値（NaN・無限大）は、無音として扱う（ピークの計算にも入れない）。
//
//   FPcmBuffer Cut;
//   NPcmEdit::Slice( Pcm, NPcmEdit::SnapToZeroCrossing( Pcm, 12000, 200 ), 48000, Cut );   // 切り口が、ゼロを横切る所になるようにして切り出す
//   NPcmEdit::Normalize( Cut, 0.9f );                                                          // いちばん大きい所が 0.9 になるように
//   NPcmEdit::FadeIn( Cut, 480 );  NPcmEdit::FadeOut( Cut, 960 );                              // プチッという音を防ぐ
//=============================================================================
struct FFrameRange
{
	int64 Start = 0;
	int64 End = 0;

	int64 GetLength() const { return End > Start ? End - Start : 0; }
	bool IsEmpty() const { return End <= Start; }
};

enum class EPcmFadeCurve : uint8
{
	Linear,   // 直線
	Smooth,   // 始めと終わりがなめらか（コサイン）
};

namespace NPcmEdit
{
	// In の [Start, End) を、Out に切り出す（サンプルレートとチャンネル数は引き継ぐ）。In が音として使えない・範囲が空なら false で、Out は変えない
	bool Slice( const FPcmBuffer& In, int64 Start, int64 End, FPcmBuffer& Out );

	// 範囲の中の、絶対値がいちばん大きいサンプル（全チャンネル。範囲が空なら 0）。End が負なら、最後まで
	float32 GetPeak( const FPcmBuffer& Pcm, int64 Start = 0, int64 End = -1 );

	// In の [Start, End) を削る。切り口のプチッという音を防ぐため、CrossfadeFrames フレームだけ、削る前の末尾と、削ったあとの先頭を混ぜてつなぐ
	// （混ぜた分だけ、さらに短くなる。長さは、元の長さ - 削った長さ - 混ぜたフレーム数。端を削るとき・前後が足りないときは、混ぜるフレーム数を減らす）。
	// In が使えない・範囲が空・全部を削ってしまうなら false で、Out は変えない
	bool Remove( const FPcmBuffer& In, int64 Start, int64 End, int64 CrossfadeFrames, FPcmBuffer& Out );

	// In の [Start, End) を With に置き換える（長さが変わってよい。With が空なら、そのまま削る）。
	// In が使えない・With がサンプルレートかチャンネル数が違う・結果が空なら false で、Out は変えない
	bool Replace( const FPcmBuffer& In, int64 Start, int64 End, const FPcmBuffer& With, FPcmBuffer& Out );

	// 範囲の音量を、StartGain から EndGain へ、なめらかに変える（範囲の最初のフレームが StartGain、最後のフレームが EndGain）。
	// bInDecibels が true で、2 つとも 0 より大きければ、デシベル（比）で一定の速さに変える（聞こえ方が均等）。そうでなければ直線。End が負なら、最後まで
	void ApplyGainRamp( FPcmBuffer& Pcm, float32 StartGain, float32 EndGain, int64 Start = 0, int64 End = -1, bool bInDecibels = true );

	// 範囲の音量を Gain 倍にする（End が負なら、最後まで。Gain が数でないなら、何もしない）。-1..1 を超えても切らない（保存・再生のときに切られる）
	void ApplyGain( FPcmBuffer& Pcm, float32 Gain, int64 Start = 0, int64 End = -1 );

	// いちばん大きいサンプルが TargetPeak になるように、全体の音量をそろえる。無音・TargetPeak が 0 以下・数でないなら false（何もしない）
	bool Normalize( FPcmBuffer& Pcm, float32 TargetPeak = 1.0f );

	// 先頭の Frames フレームを、無音から元の音量へ（Frames は、音の長さに切り詰める。0 以下なら、何もしない）
	void FadeIn( FPcmBuffer& Pcm, int64 Frames, EPcmFadeCurve Curve = EPcmFadeCurve::Smooth );
	// 最後の Frames フレームを、元の音量から無音へ（最後のフレームが、ちょうど無音になる）
	void FadeOut( FPcmBuffer& Pcm, int64 Frames, EPcmFadeCurve Curve = EPcmFadeCurve::Smooth );

	// 音が割れている（どれかのチャンネルの絶対値が Threshold 以上）フレームの連なりを、MinRun フレーム以上のものだけ、順に返す
	TArray<FFrameRange> FindClippedRanges( const FPcmBuffer& Pcm, float32 Threshold = 1.0f, int64 MinRun = 1 );

	// 無音（全チャンネルの絶対値が Threshold 以下）のフレームの連なりを、MinFrames フレーム以上のものだけ、順に返す
	TArray<FFrameRange> FindSilentRanges( const FPcmBuffer& Pcm, float32 Threshold, int64 MinFrames );

	// 長い無音（MinSilenceFrames フレーム以上）を、KeepFrames フレームに縮める（前半と後半を残して、間を削る）。KeepFrames 以下の無音は、そのまま。
	// 縮めた所が無ければ false で、Out は変えない。縮めた長さの合計は OutRemovedFrames に入る（省略できる）
	bool CollapseSilence( const FPcmBuffer& In, float32 Threshold, int64 MinSilenceFrames, int64 KeepFrames, FPcmBuffer& Out, int64* OutRemovedFrames = nullptr );

	// 聞こえる範囲（どれかのチャンネルの絶対値が Threshold を超える、最初のフレームから最後のフレームまで）。無音なら、空の範囲
	FFrameRange FindAudibleRange( const FPcmBuffer& Pcm, float32 Threshold = 0.001f );

	// Frame の近くで、波形がゼロを横切る（全チャンネルの平均の符号が変わる）フレームを返す。切り口・ループの端を、ここにすると、プチッという音が出にくい。
	// Frame から前後に MaxDistance フレームまで探し、近いほうを選ぶ（同じ距離なら前）。見つからなければ、Frame を音の範囲に切り詰めたもの
	int64 SnapToZeroCrossing( const FPcmBuffer& Pcm, int64 Frame, int64 MaxDistance );
}
