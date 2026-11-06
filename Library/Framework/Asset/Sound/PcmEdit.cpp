// SPDX-License-Identifier: MIT
#include "PcmEdit.h"

#include "Library/Math/MathConstants.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 範囲を、音の長さに切り詰める（End が負なら、最後まで）
	FFrameRange ClampRange( const FPcmBuffer& Pcm, int64 Start, int64 End )
	{
		const int64 Frames = Pcm.GetFrameCount();
		FFrameRange Range;
		Range.Start = NMath::Clamp<int64>( Start, 0, Frames );
		Range.End = End < 0 ? Frames : NMath::Clamp<int64>( End, 0, Frames );
		return Range;
	}

	float32 Finite( float32 Value )
	{
		return NMath::IsFinite( Value ) ? Value : 0.0f;
	}

	// フレームの、全チャンネルの平均
	float32 MixAt( const FPcmBuffer& Pcm, int64 Frame )
	{
		float32 Sum = 0.0f;
		for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
		{
			Sum += Finite( Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )] );
		}
		return Sum / static_cast<float32>( Pcm.Channels );
	}

	float32 FadeGain( float32 T, EFadeCurve Curve )
	{
		T = NMath::Clamp( T, 0.0f, 1.0f );
		return Curve == EFadeCurve::Smooth ? 0.5f - 0.5f * NMath::Cos( NMath::PI * T ) : T;
	}
}

bool NPcmEdit::Slice( const FPcmBuffer& In, int64 Start, int64 End, FPcmBuffer& Out )
{
	if ( !In.IsValid() )
	{
		return false;
	}
	const FFrameRange Range = ClampRange( In, Start, End );
	if ( Range.IsEmpty() )
	{
		return false;
	}
	FPcmBuffer Result;
	Result.SampleRate = In.SampleRate;
	Result.Channels = In.Channels;
	const int32 FirstSample = static_cast<int32>( Range.Start * In.Channels );
	const int32 NumSamples = static_cast<int32>( Range.GetLength() * In.Channels );
	Result.Samples.Reserve( NumSamples );
	for ( int32 Index = 0; Index < NumSamples; ++Index )
	{
		Result.Samples.Add( In.Samples[FirstSample + Index] );
	}
	Out = Result;
	return true;
}

float32 NPcmEdit::GetPeak( const FPcmBuffer& Pcm, int64 Start, int64 End )
{
	if ( Pcm.Channels < 1 )
	{
		return 0.0f;
	}
	const FFrameRange Range = ClampRange( Pcm, Start, End );
	float32 Peak = 0.0f;
	for ( int64 Index = Range.Start * Pcm.Channels; Index < Range.End * Pcm.Channels; ++Index )
	{
		const float32 Value = Pcm.Samples[static_cast<int32>( Index )];
		if ( NMath::IsFinite( Value ) )
		{
			Peak = NMath::Max( Peak, NMath::Abs( Value ) );
		}
	}
	return Peak;
}

void NPcmEdit::ApplyGain( FPcmBuffer& Pcm, float32 Gain, int64 Start, int64 End )
{
	if ( !NMath::IsFinite( Gain ) || Pcm.Channels < 1 )
	{
		return;
	}
	const FFrameRange Range = ClampRange( Pcm, Start, End );
	for ( int64 Index = Range.Start * Pcm.Channels; Index < Range.End * Pcm.Channels; ++Index )
	{
		float32& Sample = Pcm.Samples[static_cast<int32>( Index )];
		Sample = Finite( Sample ) * Gain;
	}
}

bool NPcmEdit::Normalize( FPcmBuffer& Pcm, float32 TargetPeak )
{
	if ( !NMath::IsFinite( TargetPeak ) || TargetPeak <= 0.0f )
	{
		return false;
	}
	const float32 Peak = GetPeak( Pcm );
	if ( Peak <= 0.0f )
	{
		return false;
	}
	ApplyGain( Pcm, TargetPeak / Peak );
	return true;
}

void NPcmEdit::FadeIn( FPcmBuffer& Pcm, int64 Frames, EFadeCurve Curve )
{
	const int64 Total = Pcm.GetFrameCount();
	Frames = NMath::Min( Frames, Total );
	for ( int64 Frame = 0; Frame < Frames; ++Frame )
	{
		const float32 Gain = FadeGain( static_cast<float32>( Frame ) / static_cast<float32>( Frames ), Curve );
		for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
		{
			float32& Sample = Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )];
			Sample = Finite( Sample ) * Gain;
		}
	}
}

void NPcmEdit::FadeOut( FPcmBuffer& Pcm, int64 Frames, EFadeCurve Curve )
{
	const int64 Total = Pcm.GetFrameCount();
	Frames = NMath::Min( Frames, Total );
	for ( int64 Offset = 0; Offset < Frames; ++Offset )
	{
		// 最後のフレームが Offset = 0（ゲイン 0）。前へ戻るほど、元の音量に近づく
		const int64 Frame = Total - 1 - Offset;
		const float32 Gain = FadeGain( static_cast<float32>( Offset ) / static_cast<float32>( Frames ), Curve );
		for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
		{
			float32& Sample = Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )];
			Sample = Finite( Sample ) * Gain;
		}
	}
}

TArray<FFrameRange> NPcmEdit::FindClippedRanges( const FPcmBuffer& Pcm, float32 Threshold, int64 MinRun )
{
	TArray<FFrameRange> Result;
	if ( !Pcm.IsValid() )
	{
		return Result;
	}
	MinRun = NMath::Max<int64>( MinRun, 1 );
	const int64 Frames = Pcm.GetFrameCount();
	int64 RunStart = -1;
	for ( int64 Frame = 0; Frame <= Frames; ++Frame )
	{
		bool bClipped = false;
		if ( Frame < Frames )
		{
			for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
			{
				const float32 Value = Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )];
				if ( NMath::IsFinite( Value ) && NMath::Abs( Value ) >= Threshold )
				{
					bClipped = true;
					break;
				}
			}
		}
		if ( bClipped )
		{
			if ( RunStart < 0 )
			{
				RunStart = Frame;
			}
		}
		else if ( RunStart >= 0 )
		{
			if ( Frame - RunStart >= MinRun )
			{
				FFrameRange Range;
				Range.Start = RunStart;
				Range.End = Frame;
				Result.Add( Range );
			}
			RunStart = -1;
		}
	}
	return Result;
}

FFrameRange NPcmEdit::FindAudibleRange( const FPcmBuffer& Pcm, float32 Threshold )
{
	FFrameRange Result;
	if ( !Pcm.IsValid() )
	{
		return Result;
	}
	const int64 Frames = Pcm.GetFrameCount();
	int64 First = -1;
	int64 Last = -1;
	for ( int64 Frame = 0; Frame < Frames; ++Frame )
	{
		for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
		{
			const float32 Value = Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )];
			if ( NMath::IsFinite( Value ) && NMath::Abs( Value ) > Threshold )
			{
				if ( First < 0 )
				{
					First = Frame;
				}
				Last = Frame;
				break;
			}
		}
	}
	if ( First >= 0 )
	{
		Result.Start = First;
		Result.End = Last + 1;
	}
	return Result;
}

int64 NPcmEdit::SnapToZeroCrossing( const FPcmBuffer& Pcm, int64 Frame, int64 MaxDistance )
{
	const int64 Frames = Pcm.GetFrameCount();
	Frame = NMath::Clamp<int64>( Frame, 0, Frames );
	if ( !Pcm.IsValid() || Frames < 2 || MaxDistance <= 0 )
	{
		return Frame;
	}
	// フレーム F（1 以上）が「横切る所」: F - 1 と F の平均の符号が変わる（0 は、正の側として扱う）。選ぶのは、2 つのうち 0 に近いほう
	const auto TryCrossing = [&]( int64 F, int64& OutFrame ) -> bool
	{
		if ( F < 1 || F >= Frames )
		{
			return false;
		}
		const float32 Before = MixAt( Pcm, F - 1 );
		const float32 After = MixAt( Pcm, F );
		if ( ( Before < 0.0f ) == ( After < 0.0f ) )
		{
			return false;
		}
		OutFrame = NMath::Abs( Before ) < NMath::Abs( After ) ? F - 1 : F;
		return true;
	};
	for ( int64 Distance = 0; Distance <= MaxDistance; ++Distance )
	{
		int64 Found = 0;
		if ( TryCrossing( Frame - Distance, Found ) )
		{
			return Found;
		}
		if ( Distance > 0 && TryCrossing( Frame + Distance, Found ) )
		{
			return Found;
		}
	}
	return Frame;
}
