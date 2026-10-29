#include "VillageArt.h"

#include "Source/Village/TileMap.h"

namespace
{
	// 疑似乱数（座標と種から決まる 0〜1。同じ入力なら同じ値）
	float32 Random01( int32 X, int32 Y, int32 Seed )
	{
		uint32 Value = static_cast<uint32>( X ) * 374761393u + static_cast<uint32>( Y ) * 668265263u + static_cast<uint32>( Seed ) * 2246822519u;
		Value = ( Value ^ ( Value >> 13 ) ) * 1274126177u;
		Value ^= Value >> 16;
		return static_cast<float32>( Value & 0xFFFFu ) / 65535.0f;
	}

	FORCEINLINE uint8 ToByte( int32 Value )
	{
		return static_cast<uint8>( NMath::Clamp( Value, 0, 255 ) );
	}

	// 色を、明るく（正の値）・暗く（負の値）する
	FColor32 Shade( const FColor32& Color, int32 Amount )
	{
		return FColor32( ToByte( Color.r + Amount ), ToByte( Color.g + Amount ), ToByte( Color.b + Amount ), Color.a );
	}

	// 円を塗る（中心 (CenterX, CenterY)、半径 Radius。画素の中心が円の中にあれば塗る）
	void FillCircle( FPixelImage& Image, float32 CenterX, float32 CenterY, float32 Radius, const FColor32& Color )
	{
		const int32 MinX = NMath::RoundToInt( CenterX - Radius ) - 1;
		const int32 MaxX = NMath::RoundToInt( CenterX + Radius ) + 1;
		const int32 MinY = NMath::RoundToInt( CenterY - Radius ) - 1;
		const int32 MaxY = NMath::RoundToInt( CenterY + Radius ) + 1;
		for ( int32 Y = MinY; Y <= MaxY; ++Y )
		{
			for ( int32 X = MinX; X <= MaxX; ++X )
			{
				const float32 DeltaX = static_cast<float32>( X ) + 0.5f - CenterX;
				const float32 DeltaY = static_cast<float32>( Y ) + 0.5f - CenterY;
				if ( DeltaX * DeltaX + DeltaY * DeltaY <= Radius * Radius )
				{
					Image.Set( X, Y, Color );
				}
			}
		}
	}

	// 草の地面（タイルの左上が (OriginX, 0)）
	void PaintGrass( FPixelImage& Image, int32 OriginX, int32 Seed )
	{
		const FColor32 Base( 74, 152, 72 );
		for ( int32 Y = 0; Y < NVillageArt::TileSize; ++Y )
		{
			for ( int32 X = 0; X < NVillageArt::TileSize; ++X )
			{
				const float32 Noise = Random01( X, Y, Seed );
				FColor32 Color = Shade( Base, NMath::RoundToInt( ( Noise - 0.5f ) * 18.0f ) );
				if ( Noise > 0.94f )
				{
					Color = FColor32( 112, 188, 94 );
				}
				Image.Set( OriginX + X, Y, Color );
			}
		}
	}

	// 草の地面に、花を数本
	void PaintFlowers( FPixelImage& Image, int32 OriginX )
	{
		PaintGrass( Image, OriginX, 11 );
		const FColor32 Petals[] = { FColor32( 250, 230, 80 ), FColor32( 250, 250, 250 ), FColor32( 240, 130, 170 ), FColor32( 150, 150, 250 ) };
		for ( int32 Index = 0; Index < 6; ++Index )
		{
			const int32 X = 3 + NMath::RoundToInt( Random01( Index, 1, 21 ) * 25.0f );
			const int32 Y = 3 + NMath::RoundToInt( Random01( Index, 2, 21 ) * 25.0f );
			const FColor32 Petal = Petals[Index % 4];
			Image.Set( OriginX + X, Y - 1, Petal );
			Image.Set( OriginX + X, Y + 1, Petal );
			Image.Set( OriginX + X - 1, Y, Petal );
			Image.Set( OriginX + X + 1, Y, Petal );
			Image.Set( OriginX + X, Y, FColor32( 240, 190, 40 ) );
		}
	}

	// 土の道
	void PaintPath( FPixelImage& Image, int32 OriginX )
	{
		const FColor32 Base( 178, 142, 98 );
		for ( int32 Y = 0; Y < NVillageArt::TileSize; ++Y )
		{
			for ( int32 X = 0; X < NVillageArt::TileSize; ++X )
			{
				const float32 Noise = Random01( X, Y, 31 );
				FColor32 Color = Shade( Base, NMath::RoundToInt( ( Noise - 0.5f ) * 22.0f ) );
				if ( Noise > 0.96f )
				{
					Color = FColor32( 206, 176, 132 );
				}
				else if ( Noise < 0.03f )
				{
					Color = FColor32( 136, 106, 74 );
				}
				Image.Set( OriginX + X, Y, Color );
			}
		}
	}

	// 水（Phase を変えた 2 枚を、交互に見せて、さざ波にする）
	void PaintWater( FPixelImage& Image, int32 OriginX, float32 Phase )
	{
		for ( int32 Y = 0; Y < NVillageArt::TileSize; ++Y )
		{
			for ( int32 X = 0; X < NVillageArt::TileSize; ++X )
			{
				const float32 Wave = NMath::Sin( ( static_cast<float32>( X ) + Phase ) * 0.4f + static_cast<float32>( Y ) * 0.7f );
				FColor32 Color( 46, 104, 186 );
				if ( Wave > 0.86f )
				{
					Color = FColor32( 124, 176, 232 );
				}
				else if ( Wave < -0.7f )
				{
					Color = FColor32( 38, 90, 170 );
				}
				Image.Set( OriginX + X, Y, Color );
			}
		}
	}

	// 木（草の地面に、幹と、丸い葉）
	void PaintTree( FPixelImage& Image, int32 OriginX )
	{
		PaintGrass( Image, OriginX, 41 );
		Image.FillRect( OriginX + 13, 19, 6, 12, FColor32( 110, 72, 42 ) );
		FillCircle( Image, static_cast<float32>( OriginX ) + 16.0f, 13.0f, 12.0f, FColor32( 30, 102, 44 ) );
		FillCircle( Image, static_cast<float32>( OriginX ) + 10.0f, 15.0f, 7.0f, FColor32( 40, 120, 52 ) );
		FillCircle( Image, static_cast<float32>( OriginX ) + 22.0f, 15.0f, 7.0f, FColor32( 40, 120, 52 ) );
		FillCircle( Image, static_cast<float32>( OriginX ) + 12.0f, 8.0f, 3.0f, FColor32( 84, 164, 84 ) );
	}

	// 屋根
	void PaintRoof( FPixelImage& Image, int32 OriginX )
	{
		for ( int32 Y = 0; Y < NVillageArt::TileSize; ++Y )
		{
			for ( int32 X = 0; X < NVillageArt::TileSize; ++X )
			{
				const int32 Row = Y / 8;
				const int32 Offset = ( Row % 2 ) * 8;
				FColor32 Color( 178, 64, 54 );
				if ( Y % 8 == 7 || ( X + Offset ) % 16 == 15 )
				{
					Color = FColor32( 128, 40, 36 );
				}
				else if ( Y % 8 == 0 )
				{
					Color = FColor32( 200, 88, 74 );
				}
				Image.Set( OriginX + X, Y, Color );
			}
		}
	}

	// 壁（窓つき）
	void PaintWall( FPixelImage& Image, int32 OriginX )
	{
		Image.FillRect( OriginX, 0, NVillageArt::TileSize, NVillageArt::TileSize, FColor32( 224, 204, 166 ) );
		Image.FillRect( OriginX, 28, NVillageArt::TileSize, 4, FColor32( 168, 150, 118 ) );
		Image.FillRect( OriginX + 8, 6, 16, 14, FColor32( 110, 80, 50 ) );
		Image.FillRect( OriginX + 10, 8, 12, 10, FColor32( 96, 156, 214 ) );
		Image.FillRect( OriginX + 15, 8, 2, 10, FColor32( 110, 80, 50 ) );
		Image.FillRect( OriginX + 10, 12, 12, 2, FColor32( 110, 80, 50 ) );
	}

	// 壁（扉つき）
	void PaintDoor( FPixelImage& Image, int32 OriginX )
	{
		Image.FillRect( OriginX, 0, NVillageArt::TileSize, NVillageArt::TileSize, FColor32( 224, 204, 166 ) );
		Image.FillRect( OriginX, 28, NVillageArt::TileSize, 4, FColor32( 168, 150, 118 ) );
		Image.FillRect( OriginX + 7, 4, 18, 28, FColor32( 96, 60, 34 ) );
		Image.FillRect( OriginX + 9, 6, 14, 26, FColor32( 124, 82, 48 ) );
		Image.FillRect( OriginX + 19, 18, 3, 3, FColor32( 236, 202, 72 ) );
	}

	// キャラクターの 1 コマ。Direction: 0 は下向き、1 は上向き、2 は右向き。Frame: 0 と 1 で、足が交互に出る
	void PaintCharacter( FPixelImage& Image, int32 OriginX, int32 Direction, int32 Frame, const FColor32& Shirt, const FColor32& Hair )
	{
		const FColor32 Skin( 240, 205, 170 );
		const FColor32 Trousers( 62, 66, 100 );
		const FColor32 Sleeve = Shade( Shirt, -30 );
		const float32 Left = static_cast<float32>( OriginX );

		// 足
		if ( Direction == 2 )
		{
			const int32 Step = Frame == 0 ? -2 : 2;
			Image.FillRect( OriginX + 9 + Step, 22, 4, 9, Trousers );
			Image.FillRect( OriginX + 12 - Step, 22, 4, 9, Shade( Trousers, -16 ) );
		}
		else
		{
			const int32 LeftEnd = Frame == 0 ? 31 : 29;
			const int32 RightEnd = Frame == 0 ? 29 : 31;
			Image.FillRect( OriginX + 8, 22, 4, LeftEnd - 22 + 1, Trousers );
			Image.FillRect( OriginX + 13, 22, 4, RightEnd - 22 + 1, Trousers );
		}

		// 体と腕
		Image.FillRect( OriginX + 7, 12, 10, 11, Shirt );
		if ( Direction == 2 )
		{
			Image.FillRect( OriginX + 10, 13, 4, 8, Sleeve );
		}
		else
		{
			Image.FillRect( OriginX + 5, 13, 2, 8, Sleeve );
			Image.FillRect( OriginX + 17, 13, 2, 8, Sleeve );
		}

		// 頭と髪
		FillCircle( Image, Left + 12.0f, 8.0f, 6.0f, Skin );
		if ( Direction == 1 )
		{
			FillCircle( Image, Left + 12.0f, 8.0f, 6.0f, Hair );
		}
		else
		{
			Image.FillRect( OriginX + 6, 2, 12, 4, Hair );
			Image.FillRect( OriginX + 6, 4, 2, 4, Hair );
			if ( Direction == 0 )
			{
				Image.FillRect( OriginX + 16, 4, 2, 4, Hair );
			}
			else
			{
				Image.FillRect( OriginX + 6, 4, 5, 5, Hair );
			}
		}

		// 目
		if ( Direction == 0 )
		{
			Image.FillRect( OriginX + 9, 9, 2, 2, FColor32( 36, 36, 52 ) );
			Image.FillRect( OriginX + 14, 9, 2, 2, FColor32( 36, 36, 52 ) );
		}
		else if ( Direction == 2 )
		{
			Image.FillRect( OriginX + 15, 9, 2, 2, FColor32( 36, 36, 52 ) );
		}
	}
}

namespace NVillageArt
{
	FPixelImage MakeTileset()
	{
		FPixelImage Image( TileSize * static_cast<int32>( EVillageTile::Count ), TileSize );
		const auto OriginOf = []( EVillageTile Tile )
		{
			return static_cast<int32>( Tile ) * TileSize;
		};
		PaintGrass( Image, OriginOf( EVillageTile::Grass ), 1 );
		PaintFlowers( Image, OriginOf( EVillageTile::Flowers ) );
		PaintPath( Image, OriginOf( EVillageTile::Path ) );
		PaintWater( Image, OriginOf( EVillageTile::WaterA ), 0.0f );
		PaintWater( Image, OriginOf( EVillageTile::WaterB ), 3.0f );
		PaintTree( Image, OriginOf( EVillageTile::Tree ) );
		PaintRoof( Image, OriginOf( EVillageTile::Roof ) );
		PaintWall( Image, OriginOf( EVillageTile::Wall ) );
		PaintDoor( Image, OriginOf( EVillageTile::Door ) );
		return Image;
	}

	FPixelImage MakeCharacter( const FColor32& Shirt, const FColor32& Hair )
	{
		FPixelImage Image( CharacterWidth * CharacterCells, CharacterHeight );
		for ( int32 Direction = 0; Direction < 3; ++Direction )
		{
			for ( int32 Frame = 0; Frame < 2; ++Frame )
			{
				PaintCharacter( Image, ( Direction * 2 + Frame ) * CharacterWidth, Direction, Frame, Shirt, Hair );
			}
		}
		return Image;
	}

	FPixelImage MakeCoin()
	{
		FPixelImage Image( CoinSize, CoinSize );
		FillCircle( Image, 8.0f, 8.0f, 7.5f, FColor32( 190, 138, 20 ) );
		FillCircle( Image, 8.0f, 8.0f, 6.0f, FColor32( 255, 214, 64 ) );
		Image.FillRect( 5, 4, 2, 5, FColor32( 255, 244, 170 ) );
		Image.FillRect( 7, 5, 2, 6, FColor32( 204, 152, 30 ) );
		return Image;
	}

	FPixelImage MakeGlow( int32 Size )
	{
		FPixelImage Image( Size, Size );
		const float32 Center = static_cast<float32>( Size ) * 0.5f;
		for ( int32 Y = 0; Y < Size; ++Y )
		{
			for ( int32 X = 0; X < Size; ++X )
			{
				const float32 DeltaX = ( static_cast<float32>( X ) + 0.5f - Center ) / Center;
				const float32 DeltaY = ( static_cast<float32>( Y ) + 0.5f - Center ) / Center;
				const float32 Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY );
				const float32 Falloff = NMath::Saturate( 1.0f - Distance );
				Image.Set( X, Y, FColor32( 255, 255, 255, ToByte( NMath::RoundToInt( Falloff * Falloff * 255.0f ) ) ) );
			}
		}
		return Image;
	}

	FPixelImage MakeRing( int32 Size, int32 Thickness )
	{
		FPixelImage Image( Size, Size );
		const float32 Center = static_cast<float32>( Size ) * 0.5f;
		const float32 OuterRadius = Center;
		const float32 InnerRadius = Center - static_cast<float32>( Thickness );
		for ( int32 Y = 0; Y < Size; ++Y )
		{
			for ( int32 X = 0; X < Size; ++X )
			{
				const float32 DeltaX = static_cast<float32>( X ) + 0.5f - Center;
				const float32 DeltaY = static_cast<float32>( Y ) + 0.5f - Center;
				const float32 Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY );
				const float32 Coverage = NMath::Saturate( OuterRadius - Distance + 0.5f ) * NMath::Saturate( Distance - InnerRadius + 0.5f );
				Image.Set( X, Y, FColor32( 255, 255, 255, ToByte( NMath::RoundToInt( Coverage * 255.0f ) ) ) );
			}
		}
		return Image;
	}
}
