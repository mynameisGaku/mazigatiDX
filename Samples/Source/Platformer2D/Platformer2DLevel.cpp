#include "Platformer2DLevel.h"

namespace
{
	bool IsSolidTile( char Tile )
	{
		return Tile == '#' || Tile == 'B' || Tile == '?' || Tile == 'M' || Tile == '*' || Tile == 'U' || Tile == 'p';
	}
}

int32 FPlatformer2DLevel::ToTile( float32 Pixel )
{
	return static_cast<int32>( NMath::Floor( Pixel / static_cast<float32>( TileSize ) ) );
}

char FPlatformer2DLevel::Get( int32 X, int32 Y ) const
{
	if ( X < 0 || X >= Width )
	{
		return '#';
	}
	if ( Y < 0 || Y >= Height )
	{
		return ' ';
	}
	return Tiles[Y * Width + X];
}

void FPlatformer2DLevel::Set( int32 X, int32 Y, char Tile )
{
	if ( X >= 0 && X < Width && Y >= 0 && Y < Height )
	{
		Tiles[Y * Width + X] = Tile;
	}
}

bool FPlatformer2DLevel::IsSolid( int32 X, int32 Y ) const
{
	return IsSolidTile( Get( X, Y ) );
}

void FPlatformer2DLevel::Fill( int32 X0, int32 Y0, int32 X1, int32 Y1, char Tile )
{
	for ( int32 Y = Y0; Y <= Y1; ++Y )
	{
		for ( int32 X = X0; X <= X1; ++X )
		{
			Set( X, Y, Tile );
		}
	}
}

FTile2DMoveResult FPlatformer2DLevel::Move( FVector2& Position, float32 HalfWidth, float32 Height, FVector2& Velocity, float32 DeltaTime ) const
{
	FTile2DMoveResult Result;
	const float32 Tile = static_cast<float32>( TileSize );

	// 横
	Position.x += Velocity.x * DeltaTime;
	{
		const int32 Top = ToTile( Position.y - Height );
		const int32 Bottom = ToTile( Position.y - 0.01f );
		if ( Velocity.x > 0.0f )
		{
			const int32 Column = ToTile( Position.x + HalfWidth );
			for ( int32 Row = Top; Row <= Bottom; ++Row )
			{
				if ( IsSolid( Column, Row ) )
				{
					Position.x = static_cast<float32>( Column ) * Tile - HalfWidth;
					Velocity.x = 0.0f;
					Result.bHitWall = true;
					break;
				}
			}
		}
		else if ( Velocity.x < 0.0f )
		{
			const int32 Column = ToTile( Position.x - HalfWidth );
			for ( int32 Row = Top; Row <= Bottom; ++Row )
			{
				if ( IsSolid( Column, Row ) )
				{
					Position.x = static_cast<float32>( Column + 1 ) * Tile + HalfWidth;
					Velocity.x = 0.0f;
					Result.bHitWall = true;
					break;
				}
			}
		}
	}

	// 縦
	Position.y += Velocity.y * DeltaTime;
	{
		const int32 Left = ToTile( Position.x - HalfWidth + 0.01f );
		const int32 Right = ToTile( Position.x + HalfWidth - 0.01f );
		if ( Velocity.y > 0.0f )
		{
			const int32 Row = ToTile( Position.y );
			for ( int32 Column = Left; Column <= Right; ++Column )
			{
				if ( IsSolid( Column, Row ) )
				{
					Position.y = static_cast<float32>( Row ) * Tile;
					Velocity.y = 0.0f;
					Result.bOnGround = true;
					break;
				}
			}
		}
		else if ( Velocity.y < 0.0f )
		{
			const int32 Row = ToTile( Position.y - Height );
			// 当たったタイルのうち、箱の中心にいちばん近いものを、叩いたことにする
			const int32 CenterColumn = ToTile( Position.x );
			int32 Best = -1;
			for ( int32 Column = Left; Column <= Right; ++Column )
			{
				if ( IsSolid( Column, Row ) )
				{
					const int32 Distance = Column > CenterColumn ? Column - CenterColumn : CenterColumn - Column;
					if ( Best < 0 || Distance < ( Best > CenterColumn ? Best - CenterColumn : CenterColumn - Best ) )
					{
						Best = Column;
					}
				}
			}
			if ( Best >= 0 )
			{
				Position.y = static_cast<float32>( Row + 1 ) * Tile + Height;
				Velocity.y = 0.0f;
				Result.bHitCeiling = true;
				Result.CeilingTileX = Best;
				Result.CeilingTileY = Row;
			}
		}
	}
	return Result;
}

bool FPlatformer2DLevel::FindTileTouching( char Tile, const FVector2& Position, float32 HalfWidth, float32 Height, int32& OutX, int32& OutY ) const
{
	const int32 Left = ToTile( Position.x - HalfWidth );
	const int32 Right = ToTile( Position.x + HalfWidth );
	const int32 Top = ToTile( Position.y - Height );
	const int32 Bottom = ToTile( Position.y - 0.01f );
	for ( int32 Row = Top; Row <= Bottom; ++Row )
	{
		for ( int32 Column = Left; Column <= Right; ++Column )
		{
			if ( Get( Column, Row ) == Tile )
			{
				OutX = Column;
				OutY = Row;
				return true;
			}
		}
	}
	return false;
}

ETile2DHit FPlatformer2DLevel::Hit( int32 X, int32 Y, bool bBig )
{
	const char Tile = Get( X, Y );
	ETile2DHit Result = ETile2DHit::None;
	switch ( Tile )
	{
	case '?':
		Set( X, Y, 'U' );
		Result = ETile2DHit::Coin;
		break;
	case 'M':
		Set( X, Y, 'U' );
		Result = ETile2DHit::PowerUp;
		break;
	case '*':
		Set( X, Y, 'U' );
		Result = ETile2DHit::Star;
		break;
	case 'B':
		if ( bBig )
		{
			Set( X, Y, ' ' );
			return ETile2DHit::Broke;
		}
		Result = ETile2DHit::Bumped;
		break;
	default:
		return ETile2DHit::None;
	}
	FTile2DBump Bump;
	Bump.X = X;
	Bump.Y = Y;
	Bumps.Add( Bump );
	return Result;
}

void FPlatformer2DLevel::Tick( float32 DeltaTime )
{
	for ( int32 Index = Bumps.Num() - 1; Index >= 0; --Index )
	{
		Bumps[Index].Time += DeltaTime;
		if ( Bumps[Index].Time >= 0.2f )
		{
			Bumps.RemoveAt( Index );
		}
	}
}

FPlatformer2DLevel FPlatformer2DLevel::MakeDefault()
{
	FPlatformer2DLevel Level;
	Level.Width = 132;
	Level.Height = 22;
	Level.Tiles.SetNum( Level.Width * Level.Height );
	for ( int32 Index = 0; Index < Level.Tiles.Num(); ++Index )
	{
		Level.Tiles[Index] = ' ';
	}

	// 地面（高さ 3 タイル。行 19〜21）。穴は、あとで抜く
	const int32 GroundTop = 19;
	Level.Fill( 0, GroundTop, Level.Width - 1, Level.Height - 1, '#' );
	Level.Fill( 41, GroundTop, 43, Level.Height - 1, ' ' );
	Level.Fill( 70, GroundTop, 73, Level.Height - 1, ' ' );
	Level.Fill( 96, GroundTop, 98, Level.Height - 1, ' ' );

	// 最初の広場: コインのブロック、キノコ（のち、花）のブロック、レンガ
	Level.Set( 12, 15, '?' );
	Level.Fill( 15, 15, 19, 15, 'B' );
	Level.Set( 16, 15, 'M' );
	Level.Set( 18, 15, '?' );
	Level.Set( 17, 11, '?' );
	for ( int32 X = 13; X <= 17; ++X )
	{
		Level.Set( X, 12 + ( X % 2 ), 'o' );
	}

	// 土管（幅 2、高さ 2 / 3 / 4）
	const int32 PipeX[3] = { 24, 30, 33 };
	const int32 PipeHeight[3] = { 2, 3, 4 };
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		Level.Fill( PipeX[Index], GroundTop - PipeHeight[Index], PipeX[Index] + 1, GroundTop - 1, 'p' );
	}

	// 穴の上の足場とコイン
	Level.Fill( 44, 15, 46, 15, 'B' );
	for ( int32 X = 40; X <= 43; ++X )
	{
		Level.Set( X, 14, 'o' );
	}

	// 中盤: 階段、スターのブロック、レンガの並び
	Level.Fill( 49, 18, 49, 18, '#' );
	Level.Fill( 50, 17, 50, 18, '#' );
	Level.Fill( 51, 16, 51, 18, '#' );
	Level.Fill( 56, 15, 60, 15, 'B' );
	Level.Set( 58, 15, '*' );
	Level.Set( 57, 11, '?' );
	Level.Set( 59, 11, 'M' );
	Level.Fill( 64, 15, 66, 15, 'B' );
	Level.Set( 65, 15, '?' );
	for ( int32 X = 69; X <= 74; ++X )
	{
		Level.Set( X, 13, 'o' );
	}

	// 後半: 土管、穴、階段（上り・下り）
	Level.Fill( 80, GroundTop - 3, 81, GroundTop - 1, 'p' );
	Level.Fill( 86, 15, 89, 15, 'B' );
	Level.Set( 87, 15, 'M' );
	Level.Set( 88, 15, '?' );
	for ( int32 Step = 0; Step < 4; ++Step )
	{
		Level.Fill( 100 + Step, GroundTop - 1 - Step, 100 + Step, GroundTop - 1, '#' );
	}
	for ( int32 Step = 0; Step < 4; ++Step )
	{
		Level.Fill( 108 + Step, GroundTop - 4 + Step, 108 + Step, GroundTop - 1, '#' );
	}
	for ( int32 X = 114; X <= 118; ++X )
	{
		Level.Set( X, 15, 'o' );
	}

	// ゴール: ポール（高さ 9 タイル）と、土台
	const int32 GoalColumn = 124;
	Level.Fill( GoalColumn, GroundTop - 9, GoalColumn, GroundTop - 1, 'G' );
	Level.Set( GoalColumn - 1, GroundTop - 1, '#' );
	Level.Fill( GoalColumn - 1, GroundTop - 1, GoalColumn - 1, GroundTop - 1, '#' );
	Level.GoalX = static_cast<float32>( GoalColumn * TileSize + TileSize / 2 );

	// 敵（足元の中心）。w = 歩く敵、s = 甲羅の敵
	const auto AddEnemy = [&Level]( char Kind, int32 TileX )
	{
		FTile2DSpawn Spawn;
		Spawn.Kind = Kind;
		Spawn.Position = FVector2( static_cast<float32>( TileX * TileSize + TileSize / 2 ), static_cast<float32>( ( 19 ) * TileSize ) );
		Level.Spawns.Add( Spawn );
	};
	AddEnemy( 'w', 20 );
	AddEnemy( 'w', 27 );
	AddEnemy( 'w', 47 );
	AddEnemy( 's', 54 );
	AddEnemy( 'w', 55 );
	AddEnemy( 'w', 62 );
	AddEnemy( 'w', 77 );
	AddEnemy( 's', 84 );
	AddEnemy( 'w', 90 );
	AddEnemy( 'w', 85 );
	AddEnemy( 'w', 104 );
	AddEnemy( 'w', 112 );
	AddEnemy( 's', 118 );

	Level.PlayerStart = FVector2( static_cast<float32>( 3 * TileSize ), static_cast<float32>( GroundTop * TileSize ) );
	return Level;
}
