// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/Encoding/ColorQuantizer.h"

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Sort.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 使われている色 1 つと、その画素の数
	struct FColorCount
	{
		uint32 Rgb = 0;
		int32 Count = 0;
	};

	FORCEINLINE int32 Channel( uint32 Rgb, int32 Axis )
	{
		return static_cast<int32>( ( Rgb >> ( 16 - Axis * 8 ) ) & 0xFF );
	}

	// 色の箱（Colors の [Begin, End) の範囲）
	struct FColorBox
	{
		int32 Begin = 0;
		int32 End = 0;
		// いちばん広がっている軸（0 = R、1 = G、2 = B）と、その幅
		int32 Axis = 0;
		int32 Range = 0;
		int64 Pixels = 0;
	};

	void Measure( const TArray<FColorCount>& Colors, FColorBox& Box )
	{
		int32 Low[3] = { 255, 255, 255 };
		int32 High[3] = { 0, 0, 0 };
		Box.Pixels = 0;
		for ( int32 Index = Box.Begin; Index < Box.End; ++Index )
		{
			for ( int32 Axis = 0; Axis < 3; ++Axis )
			{
				const int32 Value = Channel( Colors[Index].Rgb, Axis );
				Low[Axis] = NMath::Min( Low[Axis], Value );
				High[Axis] = NMath::Max( High[Axis], Value );
			}
			Box.Pixels += Colors[Index].Count;
		}
		Box.Axis = 0;
		Box.Range = High[0] - Low[0];
		for ( int32 Axis = 1; Axis < 3; ++Axis )
		{
			if ( High[Axis] - Low[Axis] > Box.Range )
			{
				Box.Axis = Axis;
				Box.Range = High[Axis] - Low[Axis];
			}
		}
	}

	// 箱の色の平均（画素の数で重みを付ける）
	FColor32 Average( const TArray<FColorCount>& Colors, const FColorBox& Box )
	{
		int64 Sum[3] = { 0, 0, 0 };
		for ( int32 Index = Box.Begin; Index < Box.End; ++Index )
		{
			for ( int32 Axis = 0; Axis < 3; ++Axis )
			{
				Sum[Axis] += static_cast<int64>( Channel( Colors[Index].Rgb, Axis ) ) * Colors[Index].Count;
			}
		}
		const int64 Total = NMath::Max<int64>( 1, Box.Pixels );
		return FColor32( static_cast<uint8>( ( Sum[0] + Total / 2 ) / Total ), static_cast<uint8>( ( Sum[1] + Total / 2 ) / Total ), static_cast<uint8>( ( Sum[2] + Total / 2 ) / Total ), 255 );
	}
}

TArray<FColor32> NColorQuantizer::BuildPalette( TArrayView<const FColor32> Pixels, int32 MaxColors )
{
	TArray<FColor32> Palette;
	if ( Pixels.Num() <= 0 || MaxColors <= 0 )
	{
		return Palette;
	}
	TMap<uint32, int32> Counts;
	for ( int32 Index = 0; Index < Pixels.Num(); ++Index )
	{
		const FColor32& Pixel = Pixels[Index];
		const uint32 Rgb = ( static_cast<uint32>( Pixel.r ) << 16 ) | ( static_cast<uint32>( Pixel.g ) << 8 ) | Pixel.b;
		++Counts.FindOrAdd( Rgb );
	}
	TArray<FColorCount> Colors;
	Colors.Reserve( Counts.Num() );
	for ( const auto& Pair : Counts )
	{
		Colors.Add( FColorCount { Pair.Key, Pair.Value } );
	}
	// 同じ入力なら同じ表になるよう、色の値の順に並べてから始める
	NAlgo::Sort( Colors.GetData(), Colors.Num(), []( const FColorCount& A, const FColorCount& B )
	{
		return A.Rgb < B.Rgb;
	} );
	if ( Colors.Num() <= MaxColors )
	{
		for ( const FColorCount& Color : Colors )
		{
			Palette.Add( FColor32( static_cast<uint8>( Channel( Color.Rgb, 0 ) ), static_cast<uint8>( Channel( Color.Rgb, 1 ) ), static_cast<uint8>( Channel( Color.Rgb, 2 ) ), 255 ) );
		}
		return Palette;
	}

	TArray<FColorBox> Boxes;
	FColorBox All;
	All.End = Colors.Num();
	Measure( Colors, All );
	Boxes.Add( All );
	while ( Boxes.Num() < MaxColors )
	{
		// 分ける箱: 色の幅 x 画素の数がいちばん大きいもの（1 色だけの箱は分けない）
		int32 Chosen = -1;
		int64 Best = 0;
		for ( int32 Index = 0; Index < Boxes.Num(); ++Index )
		{
			const int64 Score = static_cast<int64>( Boxes[Index].Range ) * Boxes[Index].Pixels;
			if ( Boxes[Index].End - Boxes[Index].Begin > 1 && Score > Best )
			{
				Best = Score;
				Chosen = Index;
			}
		}
		if ( Chosen < 0 )
		{
			break;
		}
		FColorBox Box = Boxes[Chosen];
		const int32 Axis = Box.Axis;
		NAlgo::Sort( Colors.GetData() + Box.Begin, Box.End - Box.Begin, [ Axis ]( const FColorCount& A, const FColorCount& B )
		{
			return Channel( A.Rgb, Axis ) != Channel( B.Rgb, Axis ) ? Channel( A.Rgb, Axis ) < Channel( B.Rgb, Axis ) : A.Rgb < B.Rgb;
		} );
		// 画素の数で半分になる所で分ける（両方に 1 色以上残す）
		int64 Half = 0;
		int32 Split = Box.Begin + 1;
		for ( int32 Index = Box.Begin; Index < Box.End - 1; ++Index )
		{
			Half += Colors[Index].Count;
			Split = Index + 1;
			if ( Half * 2 >= Box.Pixels )
			{
				break;
			}
		}
		FColorBox Lower = Box;
		Lower.End = Split;
		FColorBox Upper = Box;
		Upper.Begin = Split;
		Measure( Colors, Lower );
		Measure( Colors, Upper );
		Boxes[Chosen] = Lower;
		Boxes.Add( Upper );
	}
	for ( const FColorBox& Box : Boxes )
	{
		Palette.Add( Average( Colors, Box ) );
	}
	return Palette;
}
