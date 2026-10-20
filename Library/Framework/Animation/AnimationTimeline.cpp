#include "AnimationTimeline.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float32 MinStepDuration = 1.0e-4f;
}

const char* ToString( EAnimLoopMode Mode )
{
	switch ( Mode )
	{
	case EAnimLoopMode::Once: return "Once";
	case EAnimLoopMode::Loop: return "Loop";
	case EAnimLoopMode::PingPong: return "PingPong";
	}
	return "Loop";
}

EAnimLoopMode ParseLoopMode( const FString& Text, EAnimLoopMode Default )
{
	const FString Lower = Text.ToLower();
	if ( Lower == "once" ) return EAnimLoopMode::Once;
	if ( Lower == "loop" ) return EAnimLoopMode::Loop;
	if ( Lower == "pingpong" || Lower == "ping-pong" || Lower == "ping_pong" ) return EAnimLoopMode::PingPong;
	return Default;
}

//=============================================================================
// FAnimTimeline
//=============================================================================
FAnimTimeline FAnimTimeline::Build( const TArray<float32>& Durations, EAnimLoopMode Mode )
{
	FAnimTimeline Timeline;
	Timeline.Loop = Mode;

	const int32 Num = Durations.Num();
	float32 Time = 0.0f;
	auto AddStep = [&]( int32 Source )
	{
		Time += std::max( Durations[Source], MinStepDuration );
		Timeline.StepSource.Add( Source );
		Timeline.StepEnd.Add( Time );
	};

	for ( int32 i = 0; i < Num; ++i ) AddStep( i );
	if ( Mode == EAnimLoopMode::PingPong )
	{
		// 往路の逆順（端の 2 つは重ねない）
		for ( int32 i = Num - 2; i >= 1; --i ) AddStep( i );
	}
	Timeline.Total = Time;
	return Timeline;
}

//=============================================================================
// NAnim
//=============================================================================
FAnimLocation NAnim::Locate( const FAnimTimeline& Timeline, float64 Playhead )
{
	FAnimLocation Location;
	const int32 Num = Timeline.NumSteps();
	if ( Num == 0 || Timeline.Total <= 0.0f ) return Location;

	const float64 Total = Timeline.Total;
	int64 Cycles = 0;
	float64 Time = Playhead;
	if ( Timeline.Loop == EAnimLoopMode::Once )
	{
		Time = std::clamp( Playhead, 0.0, Total );
	}
	else
	{
		const float64 CycleCount = std::floor( Playhead / Total );
		Cycles = static_cast<int64>( CycleCount );
		Time = Playhead - CycleCount * Total;
	}

	// Time が属するステップ（終わりの時刻が Time より大きい最初のもの）。Once で Time == Total のときは最後
	const float32* const Begin = Timeline.StepEnd.GetData();
	const float32* const End = Begin + Num;
	const float32* Found = std::upper_bound( Begin, End, static_cast<float32>( Time ) );
	int32 Step = static_cast<int32>( Found - Begin );
	if ( Step >= Num ) Step = Num - 1;

	Location.Step = Step;
	Location.AbsoluteStep = Cycles * Num + Step;
	return Location;
}

float32 NAnim::ToClipTime( float64 Playhead, float32 Length, EAnimLoopMode Mode )
{
	if ( Length <= 0.0f ) return 0.0f;
	const float64 L = static_cast<float64>( Length );
	switch ( Mode )
	{
	case EAnimLoopMode::Once:
		return static_cast<float32>( std::clamp( Playhead, 0.0, L ) );
	case EAnimLoopMode::Loop:
		return static_cast<float32>( Playhead - std::floor( Playhead / L ) * L );
	case EAnimLoopMode::PingPong:
	{
		const float64 Wrapped = Playhead - std::floor( Playhead / ( 2.0 * L ) ) * 2.0 * L;   // 0..2L
		return static_cast<float32>( Wrapped <= L ? Wrapped : 2.0 * L - Wrapped );
	}
	}
	return 0.0f;
}

int32 NAnim::CountMarkerCrossings( float64 FromPlayhead, float64 ToPlayhead, float32 Marker, float32 Length, EAnimLoopMode Mode )
{
	if ( Length <= 0.0f || FromPlayhead == ToPlayhead || Marker < 0.0f || Marker > Length ) return 0;
	const float64 L = static_cast<float64>( Length );
	const float64 M = static_cast<float64>( Marker );
	const bool bForward = ToPlayhead > FromPlayhead;
	const float64 Low = bForward ? FromPlayhead : ToPlayhead;
	const float64 High = bForward ? ToPlayhead : FromPlayhead;

	// 区間の端: 前進は (Low, High]、後退は [Low, High)
	auto InRange = [&]( float64 Position ) { return bForward ? ( Position > Low && Position <= High ) : ( Position >= Low && Position < High ); };

	if ( Mode == EAnimLoopMode::Once )
	{
		return InRange( M ) ? 1 : 0;
	}

	// Loop: M + k * L。PingPong: M + k * 2L と (2L - M) + k * 2L
	const float64 Period = Mode == EAnimLoopMode::PingPong ? 2.0 * L : L;
	int32 Count = 0;
	auto CountSeries = [&]( float64 Offset )
	{
		// Position = Offset + k * Period が (Low, High] にある k の数
		const float64 KFirst = std::floor( ( Low - Offset ) / Period ) + 1.0;
		const float64 KLast = std::floor( ( High - Offset ) / Period );
		// 閉じ方が前進と後退で違う分は、端にちょうど乗る場合だけ補正する
		float64 First = KFirst;
		float64 Last = KLast;
		if ( !bForward )
		{
			// [Low, High): Low ちょうどを含み、High ちょうどを含まない
			if ( std::fabs( Offset + ( KFirst - 1.0 ) * Period - Low ) < 1.0e-12 ) First = KFirst - 1.0;
			if ( std::fabs( Offset + KLast * Period - High ) < 1.0e-12 ) Last = KLast - 1.0;
		}
		if ( Last >= First ) Count += static_cast<int32>( std::min( Last - First + 1.0, static_cast<float64>( MaxEnumerated ) ) );
	};
	CountSeries( M );
	if ( Mode == EAnimLoopMode::PingPong && M > 0.0 && M < L ) CountSeries( 2.0 * L - M );
	return std::min( Count, MaxEnumerated );
}

float32 NAnim::ToNormalized( float64 Playhead, float32 Length, EAnimLoopMode Mode )
{
	return Length > 0.0f ? ToClipTime( Playhead, Length, Mode ) / Length : 0.0f;
}
