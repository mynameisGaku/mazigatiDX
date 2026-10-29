#include "TileMap.h"

namespace
{
	// 疑似乱数（座標と種から決まる 0〜1。同じ入力なら同じ値。地図がいつも同じになる）
	float32 Random01( int32 X, int32 Y, int32 Seed )
	{
		uint32 Value = static_cast<uint32>( X ) * 374761393u + static_cast<uint32>( Y ) * 668265263u + static_cast<uint32>( Seed ) * 2246822519u;
		Value = ( Value ^ ( Value >> 13 ) ) * 1274126177u;
		Value ^= Value >> 16;
		return static_cast<float32>( Value & 0xFFFFu ) / 65535.0f;
	}

	// 見回りの道すじの点（タイルの角。タイル単位）。最後の点から最初の点へも、道がつながる
	constexpr int32 RouteCorners[][2] = { { 6, 6 }, { 22, 6 }, { 50, 6 }, { 50, 30 }, { 22, 30 }, { 6, 30 } };

	// 見回りの道すじの外の道（縦の道。上の道と下の道を結ぶ）
	constexpr int32 CrossFrom[2] = { 22, 6 };
	constexpr int32 CrossTo[2] = { 22, 30 };

	// 家の左上（タイル）
	constexpr int32 HouseCorners[][2] = { { 10, 10 }, { 26, 11 }, { 10, 22 }, { 26, 22 } };

	// 街灯の足元（タイル。タイルの中央の下）
	constexpr int32 LampTiles[][2] = { { 8, 8 }, { 20, 8 }, { 24, 20 }, { 48, 8 } };

	// 池の中心（タイル）と、半径（タイル）
	constexpr float32 LakeCenterColumn = 40.0f;
	constexpr float32 LakeCenterRow = 18.0f;
	constexpr float32 LakeRadiusColumns = 8.0f;
	constexpr float32 LakeRadiusRows = 5.0f;

	// 村人の足元（タイル。道のすぐ下の、草の上）
	constexpr int32 VillagerTile[2] = { 25, 7 };

	// 金貨どうしの間隔（道すじに沿って。ピクセル）
	constexpr float32 CoinSpacing = 320.0f;
}

CTileMap::CTileMap( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CTexture>& InTileset )
	: State( InState )
	, Tileset( InTileset )
{
	Generate();
}

void CTileMap::SetTile( int32 Column, int32 Row, EVillageTile Tile )
{
	if ( Column >= 0 && Column < Columns && Row >= 0 && Row < Rows )
	{
		Tiles[Row * Columns + Column] = Tile;
	}
}

EVillageTile CTileMap::GetTile( int32 Column, int32 Row ) const
{
	if ( Column < 0 || Column >= Columns || Row < 0 || Row >= Rows )
	{
		return EVillageTile::Tree;
	}
	return Tiles[Row * Columns + Column];
}

bool CTileMap::IsBlocking( EVillageTile Tile )
{
	switch ( Tile )
	{
	case EVillageTile::WaterA:
	case EVillageTile::WaterB:
	case EVillageTile::Tree:
	case EVillageTile::Roof:
	case EVillageTile::Wall:
	case EVillageTile::Door:
		return true;
	default:
		return false;
	}
}

bool CTileMap::IsBlockedAt( float32 X, float32 Y ) const
{
	return IsBlocking( GetTile( static_cast<int32>( NMath::Floor( X / static_cast<float32>( TileSize ) ) ), static_cast<int32>( NMath::Floor( Y / static_cast<float32>( TileSize ) ) ) ) );
}

bool CTileMap::IsAreaBlocked( const FVector2& Center, float32 HalfWidth, float32 HalfHeight ) const
{
	return IsBlockedAt( Center.x - HalfWidth, Center.y - HalfHeight ) || IsBlockedAt( Center.x + HalfWidth, Center.y - HalfHeight ) || IsBlockedAt( Center.x - HalfWidth, Center.y + HalfHeight ) || IsBlockedAt( Center.x + HalfWidth, Center.y + HalfHeight );
}

void CTileMap::CarveBand( int32 FromColumn, int32 FromRow, int32 ToColumn, int32 ToRow )
{
	const int32 MinColumn = NMath::Min( FromColumn, ToColumn ) - 1;
	const int32 MaxColumn = NMath::Max( FromColumn, ToColumn );
	const int32 MinRow = NMath::Min( FromRow, ToRow ) - 1;
	const int32 MaxRow = NMath::Max( FromRow, ToRow );
	for ( int32 Row = MinRow; Row <= MaxRow; ++Row )
	{
		for ( int32 Column = MinColumn; Column <= MaxColumn; ++Column )
		{
			SetTile( Column, Row, EVillageTile::Path );
		}
	}
}

void CTileMap::PlaceHouse( int32 Column, int32 Row )
{
	for ( int32 Offset = 0; Offset < 4; ++Offset )
	{
		SetTile( Column + Offset, Row, EVillageTile::Roof );
		SetTile( Column + Offset, Row + 1, EVillageTile::Roof );
		SetTile( Column + Offset, Row + 2, Offset == 1 ? EVillageTile::Door : EVillageTile::Wall );
	}
}

bool CTileMap::HasNearby( int32 Column, int32 Row, int32 Radius ) const
{
	for ( int32 DeltaRow = -Radius; DeltaRow <= Radius; ++DeltaRow )
	{
		for ( int32 DeltaColumn = -Radius; DeltaColumn <= Radius; ++DeltaColumn )
		{
			const EVillageTile Tile = GetTile( Column + DeltaColumn, Row + DeltaRow );
			if ( Tile == EVillageTile::Path || Tile == EVillageTile::WaterA || Tile == EVillageTile::WaterB || Tile == EVillageTile::Roof || Tile == EVillageTile::Wall || Tile == EVillageTile::Door )
			{
				return true;
			}
		}
	}
	return false;
}

void CTileMap::Generate()
{
	Tiles.SetNum( Columns * Rows, EVillageTile::Grass );

	// 花
	for ( int32 Row = 0; Row < Rows; ++Row )
	{
		for ( int32 Column = 0; Column < Columns; ++Column )
		{
			if ( Random01( Column, Row, 3 ) > 0.92f )
			{
				SetTile( Column, Row, EVillageTile::Flowers );
			}
		}
	}

	// 池
	for ( int32 Row = 0; Row < Rows; ++Row )
	{
		for ( int32 Column = 0; Column < Columns; ++Column )
		{
			const float32 DeltaColumn = ( static_cast<float32>( Column ) - LakeCenterColumn ) / LakeRadiusColumns;
			const float32 DeltaRow = ( static_cast<float32>( Row ) - LakeCenterRow ) / LakeRadiusRows;
			if ( DeltaColumn * DeltaColumn + DeltaRow * DeltaRow <= 1.0f )
			{
				SetTile( Column, Row, EVillageTile::WaterA );
			}
		}
	}

	// 道すじ（見回りの道）と、縦の道
	const int32 NumCorners = static_cast<int32>( sizeof( RouteCorners ) / sizeof( RouteCorners[0] ) );
	for ( int32 Index = 0; Index < NumCorners; ++Index )
	{
		const int32* From = RouteCorners[Index];
		const int32* To = RouteCorners[( Index + 1 ) % NumCorners];
		CarveBand( From[0], From[1], To[0], To[1] );
		Route.Add( FVector2( static_cast<float32>( From[0] * TileSize ), static_cast<float32>( From[1] * TileSize ) ) );
	}
	CarveBand( CrossFrom[0], CrossFrom[1], CrossTo[0], CrossTo[1] );

	// 家
	for ( const int32* Corner : HouseCorners )
	{
		PlaceHouse( Corner[0], Corner[1] );
	}

	// 木: まわりを囲む森と、道や家から離れた場所のまばらな木
	for ( int32 Row = 0; Row < Rows; ++Row )
	{
		for ( int32 Column = 0; Column < Columns; ++Column )
		{
			const EVillageTile Tile = GetTile( Column, Row );
			if ( Column < 2 || Row < 2 || Column >= Columns - 2 || Row >= Rows - 2 )
			{
				SetTile( Column, Row, EVillageTile::Tree );
			}
			else if ( Tile == EVillageTile::Grass || Tile == EVillageTile::Flowers )
			{
				const bool bNearEdge = Column < 5 || Row < 5 || Column >= Columns - 5 || Row >= Rows - 5;
				const float32 Chance = Random01( Column, Row, 7 );
				if ( bNearEdge ? Chance < 0.5f : ( Chance < 0.1f && !HasNearby( Column, Row, 2 ) ) )
				{
					SetTile( Column, Row, EVillageTile::Tree );
				}
			}
		}
	}

	// 街灯・村人
	for ( const int32* Lamp : LampTiles )
	{
		LampPositions.Add( FVector2( static_cast<float32>( Lamp[0] * TileSize + TileSize / 2 ), static_cast<float32>( Lamp[1] * TileSize + TileSize - 2 ) ) );
	}
	VillagerPosition = FVector2( static_cast<float32>( VillagerTile[0] * TileSize + TileSize / 2 ), static_cast<float32>( VillagerTile[1] * TileSize + TileSize - 2 ) );

	// 金貨: 道すじに沿って、一定の間隔で並べる（見回りで、ぜんぶ通る）
	float32 Carried = CoinSpacing * 0.5f;
	for ( int32 Index = 0; Index < Route.Num(); ++Index )
	{
		const FVector2 From = Route[Index];
		const FVector2 To = Route[( Index + 1 ) % Route.Num()];
		const FVector2 Delta = To - From;
		const float32 Length = NMath::Abs( Delta.x ) + NMath::Abs( Delta.y );
		const FVector2 Direction = Length > 0.0f ? Delta / Length : FVector2();
		for ( ; Carried < Length; Carried += CoinSpacing )
		{
			CoinPositions.Add( From + Direction * Carried );
		}
		Carried -= Length;
	}
}

void CTileMap::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Tileset.IsValid() )
	{
		return;
	}
	FWorldScope World( CommandList, *State );

	// 見えている範囲のタイルだけを描く
	const int32 CameraX = static_cast<int32>( State->CameraPosition.x );
	const int32 CameraY = static_cast<int32>( State->CameraPosition.y );
	const int32 FirstColumn = NMath::Max( 0, CameraX / TileSize );
	const int32 LastColumn = NMath::Min( Columns - 1, ( CameraX + FVillageState::ViewWidth ) / TileSize );
	const int32 FirstRow = NMath::Max( 0, CameraY / TileSize );
	const int32 LastRow = NMath::Min( Rows - 1, ( CameraY + FVillageState::ViewHeight ) / TileSize );

	// 水は、2 枚の絵を交互に見せて、さざ波にする
	const bool bSecondWave = ( static_cast<int32>( State->Time * 2.0f ) % 2 ) == 1;
	for ( int32 Row = FirstRow; Row <= LastRow; ++Row )
	{
		for ( int32 Column = FirstColumn; Column <= LastColumn; ++Column )
		{
			EVillageTile Tile = GetTile( Column, Row );
			if ( Tile == EVillageTile::WaterA && bSecondWave )
			{
				Tile = EVillageTile::WaterB;
			}
			CommandList.ImagePart( static_cast<float32>( Column * TileSize ), static_cast<float32>( Row * TileSize ), static_cast<int32>( Tile ) * TileSize, 0, TileSize, TileSize, Tileset );
		}
	}

	// 街灯（柱と灯り。夜は、CDarkness が、まわりを照らす）
	for ( const FVector2& Lamp : LampPositions )
	{
		const int32 X = NMath::RoundToInt( Lamp.x );
		const int32 Y = NMath::RoundToInt( Lamp.y );
		CommandList.Box( X - 2, Y - 30, X + 2, Y, FColor32( 52, 52, 60 ) );
		CommandList.Circle( X, Y - 34, 6, FColor32( 255, 226, 140 ) );
		CommandList.Circle( X, Y - 34, 6, FColor32( 120, 100, 50 ), false );
	}
}

FPixelImage CTileMap::MakeOverview() const
{
	FPixelImage Image( Columns, Rows );
	for ( int32 Row = 0; Row < Rows; ++Row )
	{
		for ( int32 Column = 0; Column < Columns; ++Column )
		{
			FColor32 Color( 74, 152, 72 );
			switch ( GetTile( Column, Row ) )
			{
			case EVillageTile::Path:
				Color = FColor32( 190, 156, 110 );
				break;
			case EVillageTile::WaterA:
			case EVillageTile::WaterB:
				Color = FColor32( 54, 112, 196 );
				break;
			case EVillageTile::Tree:
				Color = FColor32( 30, 96, 44 );
				break;
			case EVillageTile::Roof:
				Color = FColor32( 178, 64, 54 );
				break;
			case EVillageTile::Wall:
			case EVillageTile::Door:
				Color = FColor32( 224, 204, 166 );
				break;
			default:
				break;
			}
			Image.Set( Column, Row, Color );
		}
	}
	return Image;
}
