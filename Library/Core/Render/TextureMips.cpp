#include "TextureMips.h"

#include "Library/Math/MathUtils.h"

namespace
{
	// 乗算済み（色 x アルファ）の、線形の光の RGBA
	struct FLinearPixel
	{
		float32 R = 0.0f;
		float32 G = 0.0f;
		float32 B = 0.0f;
		float32 A = 0.0f;
	};

	// sRGB の 8 ビットの値から、線形の光への表
	struct FSrgbTable
	{
		float32 Values[256];

		FSrgbTable()
		{
			for ( int32 Index = 0; Index < 256; ++Index )
			{
				const float32 Encoded = static_cast<float32>( Index ) / 255.0f;
				Values[Index] = Encoded <= 0.04045f ? Encoded / 12.92f : NMath::Pow( ( Encoded + 0.055f ) / 1.055f, 2.4f );
			}
		}
	};

	const float32* GetSrgbToLinear()
	{
		static const FSrgbTable Table; // 初回の呼び出しで、1 度だけ作る
		return Table.Values;
	}

	// 線形の光（0..1）から、sRGB の 0..1
	float32 LinearToSrgb( float32 Value )
	{
		Value = NMath::Clamp( Value, 0.0f, 1.0f );
		return Value <= 0.0031308f ? Value * 12.92f : 1.055f * NMath::Pow( Value, 1.0f / 2.4f ) - 0.055f;
	}

	uint8 ToByte( float32 Value )
	{
		return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f + 0.5f );
	}

	// 元の画素を、乗算済みの線形の光にする
	FLinearPixel ToLinear( const FColor32& Pixel, const float32* SrgbToLinear )
	{
		const float32 Alpha = static_cast<float32>( Pixel.a ) / 255.0f;
		FLinearPixel Result;
		Result.R = SrgbToLinear[Pixel.r] * Alpha;
		Result.G = SrgbToLinear[Pixel.g] * Alpha;
		Result.B = SrgbToLinear[Pixel.b] * Alpha;
		Result.A = Alpha;
		return Result;
	}

	// 色変換やアルファによる重み付けをせず、データの各成分を独立して扱う。
	FLinearPixel ToData( const FColor32& Pixel )
	{
		return FLinearPixel { Pixel.r / 255.0f, Pixel.g / 255.0f, Pixel.b / 255.0f, Pixel.a / 255.0f };
	}

	// 乗算済みの線形の光から、元の形式（非乗算済みの sRGB の 8 ビット）へ戻す。アルファが 0 の画素の色は 0
	FColor32 ToColor32( const FLinearPixel& Pixel )
	{
		if ( Pixel.A <= 1.0e-6f )
		{
			return FColor32( 0, 0, 0, 0 );
		}
		const float32 Inverse = 1.0f / Pixel.A;
		return FColor32( ToByte( LinearToSrgb( Pixel.R * Inverse ) ), ToByte( LinearToSrgb( Pixel.G * Inverse ) ), ToByte( LinearToSrgb( Pixel.B * Inverse ) ), ToByte( Pixel.A ) );
	}

	void AddScaled( FLinearPixel& Sum, const FLinearPixel& Value, float32 Weight )
	{
		Sum.R += Value.R * Weight;
		Sum.G += Value.G * Weight;
		Sum.B += Value.B * Weight;
		Sum.A += Value.A * Weight;
	}

	// 幅 SW・高さ SH の画像（Get( x, y ) で画素を読む）を、幅 DW・高さ DH へ、面積の重みで平均して縮める。先に横、次に縦（箱フィルターは、分けて計算できる）
	template<typename TGet>
	void Downsample( TGet Get, int32 SW, int32 SH, int32 DW, int32 DH, TArray<FLinearPixel>& Out )
	{
		TArray<FLinearPixel> Across; // 横だけ縮めた（幅 DW・高さ SH）
		Across.SetNum( DW * SH );
		const float64 StepX = static_cast<float64>( SW ) / static_cast<float64>( DW );
		const float64 StepY = static_cast<float64>( SH ) / static_cast<float64>( DH );
		for ( int32 DestX = 0; DestX < DW; ++DestX )
		{
			const float64 From = DestX * StepX;
			const float64 To = ( DestX + 1 ) * StepX;
			const int32 First = static_cast<int32>( NMath::Floor( From ) );
			const int32 Last = NMath::Min( static_cast<int32>( NMath::Ceil( To ) ), SW );
			for ( int32 Y = 0; Y < SH; ++Y )
			{
				FLinearPixel Sum;
				for ( int32 SourceX = First; SourceX < Last; ++SourceX )
				{
					const float64 Overlap = NMath::Min( To, static_cast<float64>( SourceX + 1 ) ) - NMath::Max( From, static_cast<float64>( SourceX ) );
					AddScaled( Sum, Get( SourceX, Y ), static_cast<float32>( Overlap / StepX ) );
				}
				Across[Y * DW + DestX] = Sum;
			}
		}
		Out.SetNum( DW * DH );
		for ( int32 DestY = 0; DestY < DH; ++DestY )
		{
			const float64 From = DestY * StepY;
			const float64 To = ( DestY + 1 ) * StepY;
			const int32 First = static_cast<int32>( NMath::Floor( From ) );
			const int32 Last = NMath::Min( static_cast<int32>( NMath::Ceil( To ) ), SH );
			for ( int32 X = 0; X < DW; ++X )
			{
				FLinearPixel Sum;
				for ( int32 SourceY = First; SourceY < Last; ++SourceY )
				{
					const float64 Overlap = NMath::Min( To, static_cast<float64>( SourceY + 1 ) ) - NMath::Max( From, static_cast<float64>( SourceY ) );
					AddScaled( Sum, Across[SourceY * DW + X], static_cast<float32>( Overlap / StepY ) );
				}
				Out[DestY * DW + X] = Sum;
			}
		}
	}
}

int32 NTextureMips::GetNumLevels( int32 Width, int32 Height )
{
	if ( Width <= 0 || Height <= 0 )
	{
		return 0;
	}
	int32 Longest = NMath::Max( Width, Height );
	int32 Levels = 1;
	while ( Longest > 1 )
	{
		Longest >>= 1;
		++Levels;
	}
	return Levels;
}

int32 NTextureMips::GetLevelSize( int32 Size, int32 Level )
{
	if ( Size <= 0 || Level < 0 )
	{
		return 0;
	}
	return Level >= 31 ? 1 : NMath::Max( 1, Size >> Level );
}

static bool GenerateMipChain( int32 Width, int32 Height, const FColor32* Pixels, TArray<NTextureMips::FLevel>& OutLevels, bool bLinearData )
{
	if ( Width <= 0 || Height <= 0 || !Pixels || static_cast<int64>( Width ) * Height > NTextureMips::MaxSourcePixels )
	{
		return false;
	}
	const int32 NumLevels = NTextureMips::GetNumLevels( Width, Height );
	TArray<NTextureMips::FLevel> Levels;
	Levels.SetNum( NumLevels - 1 );
	const float32* SrgbToLinear = GetSrgbToLinear();
	TArray<FLinearPixel> Previous; // 1 つ前の段（段 1 を作るときは、元の画像から直接読む）
	int32 PreviousWidth = Width;
	int32 PreviousHeight = Height;
	for ( int32 Level = 1; Level < NumLevels; ++Level )
	{
		const int32 LevelWidth = NTextureMips::GetLevelSize( Width, Level );
		const int32 LevelHeight = NTextureMips::GetLevelSize( Height, Level );
		TArray<FLinearPixel> Current;
		if ( Level == 1 )
		{
			Downsample( [&]( int32 X, int32 Y ) { return bLinearData ? ToData( Pixels[static_cast<usize>( Y ) * Width + X] ) : ToLinear( Pixels[static_cast<usize>( Y ) * Width + X], SrgbToLinear ); }, PreviousWidth, PreviousHeight, LevelWidth, LevelHeight, Current );
		}
		else
		{
			Downsample( [&]( int32 X, int32 Y ) { return Previous[static_cast<usize>( Y ) * PreviousWidth + X]; }, PreviousWidth, PreviousHeight, LevelWidth, LevelHeight, Current );
		}
		NTextureMips::FLevel& Out = Levels[Level - 1];
		Out.Width = LevelWidth;
		Out.Height = LevelHeight;
		Out.Pixels.SetNum( LevelWidth * LevelHeight );
		for ( int32 Index = 0; Index < Current.Num(); ++Index )
		{
			const FLinearPixel& Pixel = Current[Index]; // 面積平均された成分。
			Out.Pixels[Index] = bLinearData ? FColor32( ToByte( Pixel.R ), ToByte( Pixel.G ), ToByte( Pixel.B ), ToByte( Pixel.A ) ) : ToColor32( Pixel );
		}
		Previous = MoveTemp( Current );
		PreviousWidth = LevelWidth;
		PreviousHeight = LevelHeight;
	}
	OutLevels = MoveTemp( Levels );
	return true;
}

bool NTextureMips::GenerateChain( int32 Width, int32 Height, const FColor32* Pixels, TArray<FLevel>& OutLevels )
{
	return GenerateMipChain( Width, Height, Pixels, OutLevels, false );
}

bool NTextureMips::GenerateDataChain( int32 Width, int32 Height, const FColor32* Pixels, TArray<FLevel>& OutLevels )
{
	return GenerateMipChain( Width, Height, Pixels, OutLevels, true );
}
