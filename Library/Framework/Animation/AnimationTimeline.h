#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// アニメーションの時間の計算（純粋な計算だけ。DxLib・アセット・シーンには依存しない）
//   スプライトシートのアニメーション（コマの切り替え）と、3D モデルのアニメーション（連続した時間）の
//   両方が使う。同じ入力（再生位置・経過時間・速度）なら、いつ・どこで呼んでも同じ結果になる。
//=============================================================================

// 再生の終わり方
enum class EAnimLoopMode : uint8
{
	Once,       // 最後で止まる（最後のコマのまま）
	Loop,       // 最初に戻って繰り返す
	PingPong,   // 往復する（端のコマは 1 回ずつ。A B C D C B A B C ...）
};

const char* ToString( EAnimLoopMode Mode );
// "Once" / "Loop" / "PingPong"（大文字小文字は問わない）。分からなければ Default
EAnimLoopMode ParseLoopMode( const FString& Text, EAnimLoopMode Default );

//-----------------------------------------------------------------------------
// FAnimTimeline
//   コマごとの表示時間が決まっているアニメーション（スプライトシート用）の時間軸。
//   PingPong は往復分に展開される（コマ数 N なら 2N-2 ステップ）。
//   再生位置（秒）から、いまのステップとコマを調べられる。
//-----------------------------------------------------------------------------
struct FAnimTimeline
{
	EAnimLoopMode Loop = EAnimLoopMode::Loop;
	TArray<int32> StepSource;   // 再生順の各ステップが、もとの何番目のコマか
	TArray<float32> StepEnd;    // 各ステップが終わる時刻（秒、累積）
	float32 Total = 0.0f;       // 1 周の長さ（秒）

	FORCEINLINE int32 NumSteps() const { return StepSource.Num(); }
	FORCEINLINE bool IsEmpty() const { return StepSource.Num() == 0; }

	// Durations は、もとのコマごとの表示時間（秒）。極端に短い時間は下限を設ける（無限ループの防止）
	static FAnimTimeline Build( const TArray<float32>& Durations, EAnimLoopMode Mode );
};

// 再生位置が指している場所
struct FAnimLocation
{
	int32 Step = 0;           // 時間軸の何ステップ目か
	int64 AbsoluteStep = 0;   // 繰り返しを数えた通し番号（Loop / PingPong は、1 周するたびに NumSteps ずつ増える。Once は Step と同じ）
};

namespace NAnim
{
	// 再生位置（秒。0 から始まり、速度が負なら減る。巻き戻さずに足し続けてよい）が指す場所
	//   Once は [0, Total] に収める（Total 以上は最後のステップ）。Loop / PingPong は周期的に巻き戻る（負の位置もよい）
	FAnimLocation Locate( const FAnimTimeline& Timeline, float64 Playhead );

	// 連続した時間のアニメーション（3D モデル用）。再生位置（秒。0 から始まり、速度が負なら減る。巻き戻さずに足し続けてよい）から、
	// クリップの中の時間（0..Length）を求める
	//   Once: 0..Length に収める
	//   Loop: Length ごとに巻き戻る
	//   PingPong: 0..Length..0 と往復する（周期は 2 * Length）
	float32 ToClipTime( float64 Playhead, float32 Length, EAnimLoopMode Mode );
	// クリップの中の位置を 0..1 にしたもの
	float32 ToNormalized( float64 Playhead, float32 Length, EAnimLoopMode Mode );

	// クリップの中の時間 Marker（秒、0..Length）を、再生位置が (From, To]（逆再生なら [To, From)）の間に通った回数。
	//   Once は 1 回きり。Loop は Length ごと、PingPong は往路と復路で（周期 2 * Length に 2 回）数える。長い休止でも固まらないよう最大 MaxEnumerated 回まで
	int32 CountMarkerCrossings( float64 FromPlayhead, float64 ToPlayhead, float32 Marker, float32 Length, EAnimLoopMode Mode );

	// 通し番号 (From, To] の間に入った（Increasing）／出た側から入り直した（Decreasing）ステップを列挙する。
	//   From の次から To まで（To を含み From を含まない）。Decreasing のときは To から From の手前まで。
	//   呼び出された順に Function( int32 Step ) を呼ぶ。1 回の呼び出しで列挙するのは最大 MaxEnumerated 個まで（長い休止後でも固まらない）
	constexpr int32 MaxEnumerated = 256;
	template<typename TFunction>
	void ForEachEnteredStep( int64 FromAbsolute, int64 ToAbsolute, int32 NumSteps, TFunction&& Function )
	{
		if ( NumSteps <= 0 || FromAbsolute == ToAbsolute ) return;
		auto StepOf = [NumSteps]( int64 Absolute ) {
			int64 Mod = Absolute % NumSteps;
			if ( Mod < 0 ) Mod += NumSteps;
			return static_cast<int32>( Mod );
		};
		int32 Count = 0;
		if ( ToAbsolute > FromAbsolute )
		{
			// 長く止まったあとの巻き戻しは、最後の MaxEnumerated 個だけ
			int64 Begin = FromAbsolute + 1;
			if ( ToAbsolute - Begin + 1 > MaxEnumerated ) Begin = ToAbsolute - MaxEnumerated + 1;
			for ( int64 Absolute = Begin; Absolute <= ToAbsolute && Count < MaxEnumerated; ++Absolute, ++Count ) Function( StepOf( Absolute ) );
		}
		else
		{
			int64 Begin = FromAbsolute - 1;
			if ( Begin - ToAbsolute + 1 > MaxEnumerated ) Begin = ToAbsolute + MaxEnumerated - 1;
			for ( int64 Absolute = Begin; Absolute >= ToAbsolute && Count < MaxEnumerated; --Absolute, ++Count ) Function( StepOf( Absolute ) );
		}
	}
}
