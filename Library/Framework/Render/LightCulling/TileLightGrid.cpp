#include "TileLightGrid.h"

#include "Library/Framework/Render/Deferred/GBufferView.h"

namespace
{
	// タイルの 4 つの側面（カメラを通る面）の、内側を向いた法線
	struct FTilePlanes
	{
		FVector3 Normals[4];
	};

	// 向き（カメラから Far の面の点へのベクトル）の 2 つの角が張る面の法線を、タイルの中心の側へ向けて返す
	FVector3 MakeInwardNormal( const FVector3& A, const FVector3& B, const FVector3& Center )
	{
		FVector3 Normal = FVector3::Cross( A, B ).Normalized();
		if ( FVector3::Dot( Normal, Center ) < 0.0f )
		{
			Normal = Normal * -1.0f;
		}
		return Normal;
	}

	bool IsFinite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}
}

bool CTileLightGrid::Build( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 InNumLights, const FTileLightGridDesc& InDesc )
{
	Reset();
	if ( InDesc.TileSize < 8 || InDesc.TileSize > 512 || InDesc.MaxLightsPerTile < LightsPerTexel || InDesc.MaxLightsPerTile > 96 || InDesc.MaxLightsPerTile % LightsPerTexel != 0 ||
		InNumLights < 0 || InNumLights > MaxLights || ( InNumLights > 0 && Lights == nullptr ) || !Viewport.IsValid() )
	{
		return false;
	}
	FGBufferView View;
	if ( !FGBufferView::Make( Camera, Viewport, View ) )
	{
		return false;
	}
	for ( int32 Index = 0; Index < InNumLights; ++Index )
	{
		if ( !IsFinite( Lights[Index].Position ) || !NMath::IsFinite( Lights[Index].Radius ) )
		{
			return false;
		}
	}
	const int32 TilesX = ( Viewport.Width + InDesc.TileSize - 1 ) / InDesc.TileSize;
	const int32 TilesY = ( Viewport.Height + InDesc.TileSize - 1 ) / InDesc.TileSize;
	if ( TilesX * ( InDesc.MaxLightsPerTile / LightsPerTexel ) > FPixelImage::MaxSize )
	{
		return false;
	}

	// 光ごとに、カメラからの位置と、カメラの前向きの奥行きを、先に求める。Near・Far の範囲の外の光（奥行きの範囲が、重ならない光）は、どのタイルにも入れない
	const FVector3 Forward = Camera.GetForward();
	struct FLocalLight
	{
		FVector3 Offset;
		float32 Radius;
		bool bUsed;
	};
	TArray<FLocalLight> Locals;
	Locals.SetNum( InNumLights );
	for ( int32 Index = 0; Index < InNumLights; ++Index )
	{
		FLocalLight& Local = Locals[Index];
		Local.Offset = Lights[Index].Position - Camera.Position;
		Local.Radius = Lights[Index].Radius;
		const float32 Depth = FVector3::Dot( Local.Offset, Forward );
		Local.bUsed = Local.Radius > 0.0f && Depth + Local.Radius >= Camera.Near && Depth - Local.Radius <= Camera.Far;
	}

	Desc = InDesc;
	NumTilesX = TilesX;
	NumTilesY = TilesY;
	NumLights = InNumLights;
	const int32 NumTiles = TilesX * TilesY;
	Counts.SetNum( NumTiles );
	Overflowed.SetNum( NumTiles );
	Entries.SetNum( NumTiles * Desc.MaxLightsPerTile );
	for ( int32 Index = 0; Index < NumTiles; ++Index )
	{
		Counts[Index] = 0;
		Overflowed[Index] = 0;
	}
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		Entries[Index] = 0;
	}

	const float32 Width = static_cast<float32>( Viewport.Width );
	const float32 Height = static_cast<float32>( Viewport.Height );
	for ( int32 TileY = 0; TileY < TilesY; ++TileY )
	{
		for ( int32 TileX = 0; TileX < TilesX; ++TileX )
		{
			// タイルの UV の範囲（右端・下端のタイルは、画面の端まで）
			const float32 U0 = static_cast<float32>( TileX * Desc.TileSize ) / Width;
			const float32 V0 = static_cast<float32>( TileY * Desc.TileSize ) / Height;
			const float32 U1 = static_cast<float32>( NMath::Min( ( TileX + 1 ) * Desc.TileSize, Viewport.Width ) ) / Width;
			const float32 V1 = static_cast<float32>( NMath::Min( ( TileY + 1 ) * Desc.TileSize, Viewport.Height ) ) / Height;
			const FVector3 C00 = View.GetRayToFarPlane( FVector2( U0, V0 ) );
			const FVector3 C10 = View.GetRayToFarPlane( FVector2( U1, V0 ) );
			const FVector3 C01 = View.GetRayToFarPlane( FVector2( U0, V1 ) );
			const FVector3 C11 = View.GetRayToFarPlane( FVector2( U1, V1 ) );
			const FVector3 Center = C00 + C10 + C01 + C11;
			FTilePlanes Planes;
			Planes.Normals[0] = MakeInwardNormal( C00, C10, Center );
			Planes.Normals[1] = MakeInwardNormal( C10, C11, Center );
			Planes.Normals[2] = MakeInwardNormal( C11, C01, Center );
			Planes.Normals[3] = MakeInwardNormal( C01, C00, Center );

			const int32 Tile = TileY * TilesX + TileX;
			int32 Count = 0;
			for ( int32 LightIndex = 0; LightIndex < InNumLights; ++LightIndex )
			{
				const FLocalLight& Local = Locals[LightIndex];
				if ( !Local.bUsed )
				{
					continue;
				}
				// 球が、4 つの側面の内側に、少しでもかかっているか
				bool bInside = true;
				for ( int32 Plane = 0; Plane < 4 && bInside; ++Plane )
				{
					bInside = FVector3::Dot( Planes.Normals[Plane], Local.Offset ) >= -Local.Radius;
				}
				if ( !bInside )
				{
					continue;
				}
				if ( Count >= Desc.MaxLightsPerTile )
				{
					Overflowed[Tile] = 1;
					break;
				}
				Entries[Tile * Desc.MaxLightsPerTile + Count] = static_cast<uint8>( LightIndex );
				++Count;
			}
			Counts[Tile] = static_cast<uint8>( Count );
			TotalEntries += Count;
			NumOverflowed += Overflowed[Tile] != 0 ? 1 : 0;
		}
	}
	return true;
}

void CTileLightGrid::Reset()
{
	Desc = FTileLightGridDesc();
	NumTilesX = 0;
	NumTilesY = 0;
	NumLights = 0;
	NumOverflowed = 0;
	TotalEntries = 0;
	Counts.Empty();
	Overflowed.Empty();
	Entries.Empty();
}

int32 CTileLightGrid::GetTileCount( int32 TileX, int32 TileY ) const
{
	if ( TileX < 0 || TileY < 0 || TileX >= NumTilesX || TileY >= NumTilesY )
	{
		return 0;
	}
	return Counts[TileY * NumTilesX + TileX];
}

int32 CTileLightGrid::GetTileLight( int32 TileX, int32 TileY, int32 Index ) const
{
	if ( Index < 0 || Index >= GetTileCount( TileX, TileY ) )
	{
		return -1;
	}
	return Entries[( TileY * NumTilesX + TileX ) * Desc.MaxLightsPerTile + Index];
}

bool CTileLightGrid::IsTileOverflowed( int32 TileX, int32 TileY ) const
{
	if ( TileX < 0 || TileY < 0 || TileX >= NumTilesX || TileY >= NumTilesY )
	{
		return false;
	}
	return Overflowed[TileY * NumTilesX + TileX] != 0;
}

bool CTileLightGrid::WriteImage( FPixelImage& Out ) const
{
	if ( !IsValid() )
	{
		return false;
	}
	const int32 TexelsPerTile = GetTexelsPerTile();
	const int32 Width = GetImageWidth();
	const int32 Height = GetImageHeight();
	if ( !Out.IsValid() || Out.GetWidth() != Width || Out.GetHeight() != Height )
	{
		Out = FPixelImage( Width, Height, FColor32( 0, 0, 0, 255 ) );
		if ( !Out.IsValid() )
		{
			return false;
		}
	}
	for ( int32 TileY = 0; TileY < NumTilesY; ++TileY )
	{
		for ( int32 TileX = 0; TileX < NumTilesX; ++TileX )
		{
			const bool bOverflowed = IsTileOverflowed( TileX, TileY );
			const int32 Count = GetTileCount( TileX, TileY );
			for ( int32 Texel = 0; Texel < TexelsPerTile; ++Texel )
			{
				uint8 Components[LightsPerTexel] = { 0, 0, 0 };
				if ( bOverflowed && Texel == 0 )
				{
					// 溢れの印（光の番号 + 1 は 254 まで）。シェーダーが、全部の光を評価する
					Components[0] = 255;
					Components[1] = 255;
					Components[2] = 255;
				}
				else if ( !bOverflowed )
				{
					for ( int32 Component = 0; Component < LightsPerTexel; ++Component )
					{
						const int32 Index = Texel * LightsPerTexel + Component;
						if ( Index < Count )
						{
							Components[Component] = static_cast<uint8>( GetTileLight( TileX, TileY, Index ) + 1 );
						}
					}
				}
				Out.Set( TileX * TexelsPerTile + Texel, TileY, FColor32( Components[0], Components[1], Components[2], 255 ) );
			}
		}
	}
	return true;
}
