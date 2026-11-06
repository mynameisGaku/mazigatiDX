// SPDX-License-Identifier: MIT
#include "Waveform.h"

#include "Library/Math/MathUtils.h"

namespace
{
	float32 Finite( float32 Value )
	{
		return NMath::IsFinite( Value ) ? Value : 0.0f;
	}

	void Merge( FWaveformPeak& Into, const FWaveformPeak& Other )
	{
		Into.Min = NMath::Min( Into.Min, Other.Min );
		Into.Max = NMath::Max( Into.Max, Other.Max );
	}

	// 元のサンプルの [Start, End) を数える（Start < End、範囲は音の中）。Channel が負なら、全チャンネル
	FWaveformPeak ScanSamples( const FPcmBuffer& Pcm, int64 Start, int64 End, int32 Channel )
	{
		FWaveformPeak Result;
		bool bFirst = true;
		const int32 First = Channel < 0 ? 0 : Channel;
		const int32 Last = Channel < 0 ? Pcm.Channels - 1 : Channel;
		for ( int64 Frame = Start; Frame < End; ++Frame )
		{
			for ( int32 C = First; C <= Last; ++C )
			{
				const float32 Value = Finite( Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + C )] );
				if ( bFirst )
				{
					Result.Min = Value;
					Result.Max = Value;
					bFirst = false;
				}
				else
				{
					Result.Min = NMath::Min( Result.Min, Value );
					Result.Max = NMath::Max( Result.Max, Value );
				}
			}
		}
		return Result;
	}
}

void CWaveformPeaks::Reset()
{
	bBuilt = false;
	FrameCount = 0;
	Channels = 0;
	Levels.Reset();
}

void CWaveformPeaks::Build( const FPcmBuffer& Pcm )
{
	Reset();
	if ( !Pcm.IsValid() )
	{
		return;
	}
	FrameCount = Pcm.GetFrameCount();
	Channels = Pcm.Channels;
	const int64 NumBuckets0 = ( FrameCount + BaseBucketFrames - 1 ) / BaseBucketFrames;

	TArray<FWaveformPeak> Level0;
	Level0.SetNum( static_cast<int32>( NumBuckets0 * Channels ) );
	for ( int64 Bucket = 0; Bucket < NumBuckets0; ++Bucket )
	{
		const int64 Start = Bucket * BaseBucketFrames;
		const int64 End = NMath::Min<int64>( Start + BaseBucketFrames, FrameCount );
		for ( int32 Channel = 0; Channel < Channels; ++Channel )
		{
			Level0[static_cast<int32>( Bucket * Channels + Channel )] = ScanSamples( Pcm, Start, End, Channel );
		}
	}
	Levels.Add( Level0 );

	// 2 つずつ束ねて、1 つになるまで
	int64 NumBuckets = NumBuckets0;
	while ( NumBuckets > 1 )
	{
		const TArray<FWaveformPeak>& Below = Levels[Levels.Num() - 1];
		const int64 NumUp = ( NumBuckets + 1 ) / 2;
		TArray<FWaveformPeak> Up;
		Up.SetNum( static_cast<int32>( NumUp * Channels ) );
		for ( int64 Bucket = 0; Bucket < NumUp; ++Bucket )
		{
			for ( int32 Channel = 0; Channel < Channels; ++Channel )
			{
				FWaveformPeak Peak = Below[static_cast<int32>( Bucket * 2 * Channels + Channel )];
				if ( Bucket * 2 + 1 < NumBuckets )
				{
					Merge( Peak, Below[static_cast<int32>( ( Bucket * 2 + 1 ) * Channels + Channel )] );
				}
				Up[static_cast<int32>( Bucket * Channels + Channel )] = Peak;
			}
		}
		Levels.Add( Up );
		NumBuckets = NumUp;
	}
	bBuilt = true;
}

FWaveformPeak CWaveformPeaks::GetRange( const FPcmBuffer& Pcm, int64 Start, int64 End, int32 Channel ) const
{
	FWaveformPeak Result;
	if ( !bBuilt || Pcm.GetFrameCount() != FrameCount || Pcm.Channels != Channels )
	{
		return Result;
	}
	Start = NMath::Clamp<int64>( Start, 0, FrameCount );
	End = NMath::Clamp<int64>( End, 0, FrameCount );
	if ( End <= Start )
	{
		return Result;
	}
	if ( Channel >= Channels )
	{
		return Result;
	}
	// 短い範囲は、元のサンプルを数える
	if ( End - Start <= BaseBucketFrames * 2 )
	{
		return ScanSamples( Pcm, Start, End, Channel );
	}
	// 端の、バケットに満たない所は元のサンプルを、間の、丸ごと入るバケットは階層を使う
	const int64 FirstFull = ( Start + BaseBucketFrames - 1 ) / BaseBucketFrames;
	const int64 LastFull = End / BaseBucketFrames;   // 含まない
	bool bHasValue = false;
	const auto Take = [&]( const FWaveformPeak& Peak )
	{
		if ( !bHasValue )
		{
			Result = Peak;
			bHasValue = true;
		}
		else
		{
			Merge( Result, Peak );
		}
	};
	if ( FirstFull * BaseBucketFrames > Start )
	{
		Take( ScanSamples( Pcm, Start, FirstFull * BaseBucketFrames, Channel ) );
	}
	int64 Bucket = FirstFull;
	while ( Bucket < LastFull )
	{
		// この位置から使える、いちばん大きい階層（位置がそろっていて、範囲に収まるもの）
		int32 Level = 0;
		while ( Level + 1 < Levels.Num() && ( Bucket % ( static_cast<int64>( 2 ) << Level ) ) == 0 && Bucket + ( static_cast<int64>( 2 ) << Level ) <= LastFull )
		{
			++Level;
		}
		const int64 Span = static_cast<int64>( 1 ) << Level;
		const int64 Index = Bucket >> Level;
		if ( Channel < 0 )
		{
			for ( int32 C = 0; C < Channels; ++C )
			{
				Take( Levels[Level][static_cast<int32>( Index * Channels + C )] );
			}
		}
		else
		{
			Take( Levels[Level][static_cast<int32>( Index * Channels + Channel )] );
		}
		Bucket += Span;
	}
	if ( LastFull * BaseBucketFrames < End )
	{
		Take( ScanSamples( Pcm, LastFull * BaseBucketFrames, End, Channel ) );
	}
	return Result;
}

void CWaveformPeaks::GetColumns( const FPcmBuffer& Pcm, int64 Start, int64 End, int32 NumColumns, int32 Channel, TArray<FWaveformPeak>& Out ) const
{
	Out.Reset();
	if ( NumColumns <= 0 )
	{
		return;
	}
	Out.SetNum( NumColumns );
	if ( !bBuilt || End <= Start )
	{
		return;
	}
	const int64 Length = End - Start;
	for ( int32 Column = 0; Column < NumColumns; ++Column )
	{
		int64 A = Start + ( Length * Column ) / NumColumns;
		int64 B = Start + ( Length * ( Column + 1 ) ) / NumColumns;
		if ( B <= A )
		{
			B = A + 1;   // 1 フレームより細かい列は、最も近い 1 フレーム
		}
		Out[Column] = GetRange( Pcm, A, B, Channel );
	}
}
