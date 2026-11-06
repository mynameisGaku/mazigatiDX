#include "IslandTerrain.h"

namespace
{
	// 島の半径（ワールドの単位）。これより外は、海の底へ下がる
	constexpr float32 IslandRadius = 50.0f;

	// 木の数と、結晶どうしの間隔の下限
	constexpr int32 NumTrees = 30;

	// 探検家の出発点のまわりの、木を置かない広さ（半径）
	constexpr float32 ClearingRadius = 9.0f;
	constexpr float32 CrystalSpacing = 14.0f;

	// 地形の色
	const FColor32 SandColor( 222, 204, 154 );
	const FColor32 GrassColor( 92, 152, 72 );
	const FColor32 DarkGrassColor( 62, 118, 58 );
	const FColor32 RockColor( 138, 132, 124 );
	const FColor32 SeaColor( 40, 96, 150 );

	// 整数の格子点の、0〜1 の乱数（決まったシード）
	float32 Hash01( int32 X, int32 Z, uint32 Seed )
	{
		uint32 Value = static_cast<uint32>( X ) * 374761393u + static_cast<uint32>( Z ) * 668265263u + Seed * 2246822519u;
		Value = ( Value ^ ( Value >> 13 ) ) * 1274126177u;
		Value ^= Value >> 16;
		return static_cast<float32>( Value & 0xFFFFFFu ) / static_cast<float32>( 0xFFFFFFu );
	}

	// 1 つの番号から、0〜1 の乱数
	float32 Hash01( uint32 Index, uint32 Seed )
	{
		return Hash01( static_cast<int32>( Index ), 7, Seed );
	}

	float32 SmoothStep( float32 Edge0, float32 Edge1, float32 X )
	{
		const float32 T = NMath::Clamp( ( X - Edge0 ) / ( Edge1 - Edge0 ), 0.0f, 1.0f );
		return T * T * ( 3.0f - 2.0f * T );
	}

	// 値ノイズ（格子点の乱数を、なめらかに補う。0〜1）
	float32 ValueNoise( float32 X, float32 Z, uint32 Seed )
	{
		const int32 X0 = static_cast<int32>( NMath::Floor( X ) );
		const int32 Z0 = static_cast<int32>( NMath::Floor( Z ) );
		const float32 FractionX = X - static_cast<float32>( X0 );
		const float32 FractionZ = Z - static_cast<float32>( Z0 );
		const float32 U = FractionX * FractionX * ( 3.0f - 2.0f * FractionX );
		const float32 V = FractionZ * FractionZ * ( 3.0f - 2.0f * FractionZ );
		const float32 A = Hash01( X0, Z0, Seed );
		const float32 B = Hash01( X0 + 1, Z0, Seed );
		const float32 C = Hash01( X0, Z0 + 1, Seed );
		const float32 D = Hash01( X0 + 1, Z0 + 1, Seed );
		return NMath::Lerp( NMath::Lerp( A, B, U ), NMath::Lerp( C, D, U ), V );
	}

	// 島の高さ: 中央ほど高く（丘はノイズ 3 段）、半径の外は海の底へ
	float32 MakeHeight( float32 X, float32 Z )
	{
		const float32 Distance = NMath::Sqrt( X * X + Z * Z ) / IslandRadius;
		const float32 Land = 1.0f - SmoothStep( 0.55f, 1.0f, Distance );
		const float32 Hills = ValueNoise( X / 20.0f, Z / 20.0f, 11u ) * 0.6f + ValueNoise( X / 9.0f, Z / 9.0f, 23u ) * 0.3f + ValueNoise( X / 4.0f, Z / 4.0f, 37u ) * 0.1f;
		return ( 1.2f + Hills * 9.0f ) * Land - 3.0f * ( 1.0f - Land );
	}

	// 格子点の高さ（範囲の外は、端の値）
	float32 HeightAt( const FIslandState& State, int32 Column, int32 Row )
	{
		const int32 Points = FIslandState::TerrainCells + 1;
		Column = NMath::Clamp( Column, 0, Points - 1 );
		Row = NMath::Clamp( Row, 0, Points - 1 );
		return State.Heights[Row * Points + Column];
	}

	// 格子点の世界の座標
	float32 ToWorld( int32 Index )
	{
		return -FIslandState::TerrainHalfSize + static_cast<float32>( Index ) * FIslandState::CellSize;
	}

	// 格子点の法線（隣の高さの差から）
	FVector3 NormalAt( const FIslandState& State, int32 Column, int32 Row )
	{
		const float32 DeltaX = HeightAt( State, Column + 1, Row ) - HeightAt( State, Column - 1, Row );
		const float32 DeltaZ = HeightAt( State, Column, Row + 1 ) - HeightAt( State, Column, Row - 1 );
		return FVector3( -DeltaX, 2.0f * FIslandState::CellSize, -DeltaZ ).Normalized();
	}

	// 乱数の場所を、歩ける所から選ぶ。高さが Low〜High で、Avoid のどれとも Spacing 以上離れた所。見つからなければ false
	bool PickSpot( const FIslandState& State, uint32 Seed, float32 Low, float32 High, const TArray<FVector3>& Avoid, float32 Spacing, FVector3& OutSpot )
	{
		for ( uint32 Attempt = 0; Attempt < 400; ++Attempt )
		{
			const float32 X = ( Hash01( Attempt, Seed ) * 2.0f - 1.0f ) * IslandRadius * 0.85f;
			const float32 Z = ( Hash01( Attempt, Seed + 1u ) * 2.0f - 1.0f ) * IslandRadius * 0.85f;
			const float32 Height = NIslandTerrain::GetHeight( State, X, Z );
			if ( Height < Low || Height > High )
			{
				continue;
			}
			bool bFar = true;
			for ( const FVector3& Other : Avoid )
			{
				const float32 DeltaX = Other.x - X;
				const float32 DeltaZ = Other.z - Z;
				if ( DeltaX * DeltaX + DeltaZ * DeltaZ < Spacing * Spacing )
				{
					bFar = false;
					break;
				}
			}
			if ( bFar )
			{
				OutSpot = FVector3( X, Height, Z );
				return true;
			}
		}
		return false;
	}
}

void NIslandTerrain::Generate( FIslandState& State )
{
	const int32 Points = FIslandState::TerrainCells + 1;
	State.Heights.SetNum( Points * Points );
	for ( int32 Row = 0; Row < Points; ++Row )
	{
		for ( int32 Column = 0; Column < Points; ++Column )
		{
			State.Heights[Row * Points + Column] = MakeHeight( ToWorld( Column ), ToWorld( Row ) );
		}
	}

	// 探検家: 南（-z）の浜辺から、島の中へ向かって、最初に歩ける所の少し先
	for ( float32 Z = -IslandRadius; Z < 0.0f; Z += 0.5f )
	{
		if ( IsWalkable( State, 0.0f, Z ) )
		{
			State.ExplorerPosition = FVector3( 0.0f, GetHeight( State, 0.0f, Z + 1.5f ), Z + 1.5f );
			break;
		}
	}

	// 結晶: 歩ける所に、離して置く
	TArray<FVector3> Taken;
	State.Crystals.Reset();
	for ( int32 Index = 0; Index < FIslandState::NumCrystals; ++Index )
	{
		FVector3 Spot;
		if ( PickSpot( State, 100u + static_cast<uint32>( Index ) * 17u, 0.8f, 8.0f, Taken, CrystalSpacing, Spot ) )
		{
			FIslandState::FCrystal Crystal;
			Crystal.Position = Spot;
			State.Crystals.Add( Crystal );
			Taken.Add( Spot );
		}
	}

	// 木: 草の高さの所に、結晶と、ほかの木と、探検家の出発点から離して置く
	State.Trees.Reset();
	for ( int32 Index = 0; Index < NumTrees; ++Index )
	{
		FVector3 Spot;
		if ( PickSpot( State, 900u + static_cast<uint32>( Index ) * 29u, 1.4f, 6.0f, Taken, 3.5f, Spot ) && ( Spot - State.ExplorerPosition ).Length() > ClearingRadius )
		{
			FIslandState::FTree Tree;
			Tree.Position = Spot;
			Tree.Scale = 0.6f + Hash01( static_cast<uint32>( Index ), 5u ) * 0.4f;
			State.Trees.Add( Tree );
			Taken.Add( Spot );
		}
	}

	// 灯台: 北（+z）の浜辺から、島の中へ向かって、最初に歩ける所の少し先（右に寄せる）
	for ( float32 Z = IslandRadius; Z > 0.0f; Z -= 0.5f )
	{
		if ( IsWalkable( State, 10.0f, Z ) )
		{
			State.LighthousePosition = FVector3( 10.0f, GetHeight( State, 10.0f, Z - 2.0f ), Z - 2.0f );
			break;
		}
	}
}

bool NIslandTerrain::MakeBuffers( FIslandState& State )
{
	const int32 Points = FIslandState::TerrainCells + 1;
	TArray<FVertex3D> Vertices;
	Vertices.Reserve( Points * Points );
	for ( int32 Row = 0; Row < Points; ++Row )
	{
		for ( int32 Column = 0; Column < Points; ++Column )
		{
			const float32 Height = HeightAt( State, Column, Row );
			Vertices.Add( FVertex3D::Make( FVector3( ToWorld( Column ), Height, ToWorld( Row ) ), NormalAt( State, Column, Row ), GetGroundColor( Height ) ) );
		}
	}
	TArray<uint16> Indices;
	Indices.Reserve( FIslandState::TerrainCells * FIslandState::TerrainCells * 6 );
	for ( int32 Row = 0; Row < FIslandState::TerrainCells; ++Row )
	{
		for ( int32 Column = 0; Column < FIslandState::TerrainCells; ++Column )
		{
			const uint16 TopLeft = static_cast<uint16>( Row * Points + Column );
			const uint16 TopRight = static_cast<uint16>( TopLeft + 1 );
			const uint16 BottomLeft = static_cast<uint16>( TopLeft + Points );
			const uint16 BottomRight = static_cast<uint16>( BottomLeft + 1 );
			// 上から見て、表が上を向く並び（Z が増える向きが奥）
			const uint16 Square[6] = { TopLeft, BottomLeft, TopRight, TopRight, BottomLeft, BottomRight };
			for ( const uint16 Index : Square )
			{
				Indices.Add( Index );
			}
		}
	}
	State.TerrainVertices = CVertexBuffer::Create( Vertices );
	State.TerrainIndices = CIndexBuffer::Create( Indices );
	return State.TerrainVertices.IsValid() && State.TerrainIndices.IsValid();
}

FPixelImage NIslandTerrain::MakeMapImage( const FIslandState& State )
{
	const int32 Points = FIslandState::TerrainCells + 1;
	FPixelImage Image( Points, Points );
	for ( int32 Row = 0; Row < Points; ++Row )
	{
		for ( int32 Column = 0; Column < Points; ++Column )
		{
			const float32 Height = HeightAt( State, Column, Row );
			FColor32 Color = GetGroundColor( Height );
			if ( Height <= FIslandState::SeaLevel )
			{
				// 深いほど濃い青
				const float32 Shade = NMath::Clamp( 1.0f + Height / 4.0f, 0.4f, 1.0f );
				Color = FColor32( static_cast<uint8>( SeaColor.r * Shade ), static_cast<uint8>( SeaColor.g * Shade ), static_cast<uint8>( SeaColor.b * Shade ) );
			}
			// 画像の上が北（+z）
			Image.Set( Column, Points - 1 - Row, Color );
		}
	}
	return Image;
}

float32 NIslandTerrain::GetHeight( const FIslandState& State, float32 X, float32 Z )
{
	const float32 GridX = ( X + FIslandState::TerrainHalfSize ) / FIslandState::CellSize;
	const float32 GridZ = ( Z + FIslandState::TerrainHalfSize ) / FIslandState::CellSize;
	if ( State.Heights.Num() == 0 || GridX < 0.0f || GridZ < 0.0f || GridX > static_cast<float32>( FIslandState::TerrainCells ) || GridZ > static_cast<float32>( FIslandState::TerrainCells ) )
	{
		return -3.0f;
	}
	const int32 Column = static_cast<int32>( NMath::Floor( GridX ) );
	const int32 Row = static_cast<int32>( NMath::Floor( GridZ ) );
	const float32 FractionX = GridX - static_cast<float32>( Column );
	const float32 FractionZ = GridZ - static_cast<float32>( Row );
	const float32 Top = NMath::Lerp( HeightAt( State, Column, Row ), HeightAt( State, Column + 1, Row ), FractionX );
	const float32 Bottom = NMath::Lerp( HeightAt( State, Column, Row + 1 ), HeightAt( State, Column + 1, Row + 1 ), FractionX );
	return NMath::Lerp( Top, Bottom, FractionZ );
}

bool NIslandTerrain::IsWalkable( const FIslandState& State, float32 X, float32 Z )
{
	return GetHeight( State, X, Z ) > FIslandState::SeaLevel + FIslandState::ShoreMargin;
}

bool NIslandTerrain::Raycast( const FIslandState& State, const FCameraRay& Ray, FVector3& OutPoint )
{
	// 少しずつ進んで、地面（と海面の高いほう）より下に入ったら、手前との間を二分して詰める
	constexpr float32 Step = 0.25f;
	constexpr float32 MaxDistance = 250.0f;
	const auto Surface = [&]( const FVector3& Point )
	{
		return NMath::Max( GetHeight( State, Point.x, Point.z ), FIslandState::SeaLevel );
	};
	FVector3 Previous = Ray.Origin;
	for ( float32 Distance = Step; Distance <= MaxDistance; Distance += Step )
	{
		const FVector3 Point = Ray.Origin + Ray.Direction * Distance;
		if ( Point.y <= Surface( Point ) )
		{
			FVector3 Above = Previous;
			FVector3 Below = Point;
			for ( int32 Iteration = 0; Iteration < 12; ++Iteration )
			{
				const FVector3 Middle = ( Above + Below ) * 0.5f;
				if ( Middle.y <= Surface( Middle ) )
				{
					Below = Middle;
				}
				else
				{
					Above = Middle;
				}
			}
			OutPoint = Below;
			OutPoint.y = Surface( Below );
			return true;
		}
		Previous = Point;
	}
	return false;
}

FColor32 NIslandTerrain::GetGroundColor( float32 Height )
{
	if ( Height < 0.9f )
	{
		return SandColor;
	}
	if ( Height < 1.6f )
	{
		const float32 T = ( Height - 0.9f ) / 0.7f;
		return FColor32( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SandColor.r ), static_cast<float32>( GrassColor.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SandColor.g ), static_cast<float32>( GrassColor.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SandColor.b ), static_cast<float32>( GrassColor.b ), T ) ) );
	}
	if ( Height < 4.5f )
	{
		return GrassColor;
	}
	if ( Height < 6.5f )
	{
		return DarkGrassColor;
	}
	return RockColor;
}
