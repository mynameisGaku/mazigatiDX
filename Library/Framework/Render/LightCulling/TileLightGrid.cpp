#include "TileLightGrid.h"

#include "Library/Framework/Render/Deferred/GBufferView.h"
#include "Library/Framework/Render/State/Lighting.h"

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

	// 層の境目の奥行き（Boundary は 0〜NumSlices）。Near x (Far / Near)^(Boundary / NumSlices)
	float32 MakeSliceDepth( float32 Near, float32 Far, int32 NumSlices, int32 Boundary )
	{
		return Near * NMath::Pow( Far / Near, static_cast<float32>( Boundary ) / static_cast<float32>( NumSlices ) );
	}

	// 光ごとに、先に求めておく値
	struct FLocalLight
	{
		FVector3 Offset;         // カメラから光の位置へのベクトル
		FVector3 Axis;           // スポットライトの円錐の軸（単位ベクトル）
		float32 Radius = 0.0f;
		float32 CosHalf = -1.0f; // 円錐の半角の cos・sin
		float32 SinHalf = 0.0f;
		float32 Depth = 0.0f;    // 光の位置の、カメラの前向きの奥行き
		float32 DepthMin = 0.0f; // 光が届く範囲の、奥行きの範囲
		float32 DepthMax = 0.0f;
		bool bSpot = false;
		bool bUsed = false;
	};

	// 光が届く範囲が、単位ベクトル Direction の向きへ、光の位置からどこまで広がるか（範囲の内側の点 p の、dot( Direction, p - 光の位置 ) の最大値。0 以上）。
	// 点の光は球なので、いつも半径。スポットライトは、頂点から半径までの、軸から半角以内の向きの点なので、Direction が軸から半角以内なら半径、外なら、円錐の縁の向きへの広がり（半径 x cos( 軸との角 - 半角 )）。それが負なら、頂点（0）
	float32 GetExtent( const FLocalLight& Local, const FVector3& Direction )
	{
		if ( !Local.bSpot )
		{
			return Local.Radius;
		}
		const float32 CosToAxis = FVector3::Dot( Direction, Local.Axis );
		if ( CosToAxis >= Local.CosHalf )
		{
			return Local.Radius;
		}
		const float32 SinToAxis = NMath::Sqrt( NMath::Max( 0.0f, 1.0f - CosToAxis * CosToAxis ) );
		return Local.Radius * NMath::Max( 0.0f, CosToAxis * Local.CosHalf + SinToAxis * Local.SinHalf );
	}
}

bool FCullLight::CanReach( const FVector3& Point ) const
{
	const FVector3 Offset = Point - Position;
	const float32 Distance = Offset.Length();
	if ( Distance > Radius )
	{
		return false;
	}
	if ( !IsSpot() )
	{
		return true;
	}
	if ( Distance <= 0.0f )
	{
		return ConeHalfAngle > 0.0f; // 頂点は、円錐の内側とみなす
	}
	return FVector3::Dot( Offset / Distance, Direction.Normalized() ) >= NMath::Cos( ConeHalfAngle );
}

bool FCullLight::FromLight( const FLight& Light, FCullLight& OutLight )
{
	if ( !Light.IsValid() || Light.Type == EDrawLightType::Directional )
	{
		return false;
	}
	FCullLight Result;
	Result.Position = Light.Position;
	Result.Radius = Light.Range;
	if ( Light.Type == EDrawLightType::Spot )
	{
		Result.Direction = Light.Direction;
		Result.ConeHalfAngle = Light.OuterAngle * 0.5f; // FLight の角度は全角
	}
	OutLight = Result;
	return true;
}

int32 CTileLightGrid::GetSliceOfDepth( float32 Depth, float32 Near, float32 Far, int32 InNumSlices )
{
	if ( InNumSlices <= 1 )
	{
		return 0;
	}
	const float32 Scale = static_cast<float32>( InNumSlices ) / NMath::Log( Far / Near );
	const int32 Slice = static_cast<int32>( NMath::Floor( NMath::Log( NMath::Max( Depth, Near ) / Near ) * Scale ) );
	return NMath::Clamp( Slice, 0, InNumSlices - 1 );
}

bool CTileLightGrid::CanBuild( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 InNumLights, const FTileLightGridDesc& InDesc )
{
	if ( InDesc.TileSize < 8 || InDesc.TileSize > 512 || InDesc.MaxLightsPerTile < LightsPerTexel || InDesc.MaxLightsPerTile > 96 || InDesc.MaxLightsPerTile % LightsPerTexel != 0 ||
		InDesc.NumDepthSlices < 1 || InDesc.NumDepthSlices > MaxDepthSlices ||
		InNumLights < 0 || InNumLights > MaxLights || ( InNumLights > 0 && Lights == nullptr ) || !Viewport.IsValid() )
	{
		return false;
	}
	// 奥行きを分けるときは、層の境目（Near と Far の比）が決まらないと困る
	if ( InDesc.NumDepthSlices > 1 && !( Camera.Near > 0.0f && Camera.Far > Camera.Near ) )
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
		if ( !IsFinite( Lights[Index].Position ) || !NMath::IsFinite( Lights[Index].Radius ) || !NMath::IsFinite( Lights[Index].ConeHalfAngle ) )
		{
			return false;
		}
		// スポットライトは、向きが決まらないと困る（有限で、長さが 0 でない）
		if ( Lights[Index].IsSpot() && ( !IsFinite( Lights[Index].Direction ) || Lights[Index].Direction.LengthSquared() <= 0.0f ) )
		{
			return false;
		}
	}
	const int32 TilesX = ( Viewport.Width + InDesc.TileSize - 1 ) / InDesc.TileSize;
	const int32 TilesY = ( Viewport.Height + InDesc.TileSize - 1 ) / InDesc.TileSize;
	if ( TilesX * ( InDesc.MaxLightsPerTile / LightsPerTexel ) > FPixelImage::MaxSize || TilesY * InDesc.NumDepthSlices > FPixelImage::MaxSize )
	{
		return false;
	}
	return true;
}

bool CTileLightGrid::Build( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 InNumLights, const FTileLightGridDesc& InDesc )
{
	Reset();
	if ( !CanBuild( Camera, Viewport, Lights, InNumLights, InDesc ) )
	{
		return false;
	}
	FGBufferView View;
	FGBufferView::Make( Camera, Viewport, View );
	const int32 TilesX = ( Viewport.Width + InDesc.TileSize - 1 ) / InDesc.TileSize;
	const int32 TilesY = ( Viewport.Height + InDesc.TileSize - 1 ) / InDesc.TileSize;

	// 光ごとに、カメラからの位置と、光が届く範囲の奥行きの範囲を、先に求める。Near・Far の範囲の外の光（奥行きの範囲が、重ならない光）は、どのクラスタにも入れない
	const FVector3 Forward = Camera.GetForward();
	const FVector3 Backward = Forward * -1.0f;
	TArray<FLocalLight> Locals;
	Locals.SetNum( InNumLights );
	for ( int32 Index = 0; Index < InNumLights; ++Index )
	{
		FLocalLight& Local = Locals[Index];
		Local.Offset = Lights[Index].Position - Camera.Position;
		Local.Radius = Lights[Index].Radius;
		Local.bSpot = Lights[Index].IsSpot();
		if ( Local.bSpot )
		{
			Local.Axis = Lights[Index].Direction.Normalized();
			Local.CosHalf = NMath::Cos( Lights[Index].ConeHalfAngle );
			Local.SinHalf = NMath::Sin( Lights[Index].ConeHalfAngle );
		}
		Local.Depth = FVector3::Dot( Local.Offset, Forward );
		// 奥行きの範囲: 前向きへの広がりと、後ろ向きへの広がり（点の光は、どちらも半径）
		Local.DepthMax = Local.Depth + GetExtent( Local, Forward );
		Local.DepthMin = Local.Depth - GetExtent( Local, Backward );
		Local.bUsed = Local.Radius > 0.0f && ( !Local.bSpot || Lights[Index].ConeHalfAngle > 0.0f ) && Local.DepthMax >= Camera.Near && Local.DepthMin <= Camera.Far;
	}

	Desc = InDesc;
	NumTilesX = TilesX;
	NumTilesY = TilesY;
	NumSlices = InDesc.NumDepthSlices;
	NumLights = InNumLights;
	NearDepth = Camera.Near;
	FarDepth = Camera.Far;
	const int32 NumClusters = TilesX * TilesY * NumSlices;
	Counts.SetNum( NumClusters );
	Overflowed.SetNum( NumClusters );
	Entries.SetNum( NumClusters * Desc.MaxLightsPerTile );
	for ( int32 Index = 0; Index < NumClusters; ++Index )
	{
		Counts[Index] = 0;
		Overflowed[Index] = 0;
	}
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		Entries[Index] = 0;
	}

	// 層ごとの奥行きの範囲（前後に少し広げる。最初の層は手前へ、最後の層は奥へ、どこまでも）
	TArray<float32> SliceLow;
	TArray<float32> SliceHigh;
	for ( int32 Slice = 0; Slice < NumSlices; ++Slice )
	{
		SliceLow.Add( Slice == 0 ? -NMath::MaxFloat : MakeSliceDepth( NearDepth, FarDepth, NumSlices, Slice ) * ( 1.0f - SliceMargin ) );
		SliceHigh.Add( Slice == NumSlices - 1 ? NMath::MaxFloat : MakeSliceDepth( NearDepth, FarDepth, NumSlices, Slice + 1 ) * ( 1.0f + SliceMargin ) );
	}

	const float32 Width = static_cast<float32>( Viewport.Width );
	const float32 Height = static_cast<float32>( Viewport.Height );
	TArray<int32> TileLights; // タイルの 4 つの側面の内側にかかる光（光の番号の昇順）
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

			TileLights.Empty();
			for ( int32 LightIndex = 0; LightIndex < InNumLights; ++LightIndex )
			{
				const FLocalLight& Local = Locals[LightIndex];
				if ( !Local.bUsed )
				{
					continue;
				}
				// 光が届く範囲（球・扇形）が、4 つの側面の内側に、少しでもかかっているか（面の内側への広がりが、中心から面までの距離以上）
				bool bInside = true;
				for ( int32 Plane = 0; Plane < 4 && bInside; ++Plane )
				{
					bInside = FVector3::Dot( Planes.Normals[Plane], Local.Offset ) >= -GetExtent( Local, Planes.Normals[Plane] );
				}
				if ( bInside )
				{
					TileLights.Add( LightIndex );
				}
			}

			// 層ごとに、光が届く範囲の奥行きの範囲（球は、中心の奥行き ± 半径）が重なる光だけを入れる
			for ( int32 Slice = 0; Slice < NumSlices; ++Slice )
			{
				const int32 Cluster = GetClusterIndex( TileX, TileY, Slice );
				int32 Count = 0;
				for ( const int32 LightIndex : TileLights )
				{
					const FLocalLight& Local = Locals[LightIndex];
					if ( Local.DepthMax < SliceLow[Slice] || Local.DepthMin > SliceHigh[Slice] )
					{
						continue;
					}
					if ( Count >= Desc.MaxLightsPerTile )
					{
						Overflowed[Cluster] = 1;
						break;
					}
					Entries[Cluster * Desc.MaxLightsPerTile + Count] = static_cast<uint8>( LightIndex );
					++Count;
				}
				Counts[Cluster] = static_cast<uint8>( Count );
				TotalEntries += Count;
				NumOverflowed += Overflowed[Cluster] != 0 ? 1 : 0;
			}
		}
	}
	return true;
}

void CTileLightGrid::Reset()
{
	Desc = FTileLightGridDesc();
	NumTilesX = 0;
	NumTilesY = 0;
	NumSlices = 0;
	NumLights = 0;
	NumOverflowed = 0;
	TotalEntries = 0;
	NearDepth = 0.0f;
	FarDepth = 0.0f;
	Counts.Empty();
	Overflowed.Empty();
	Entries.Empty();
}

float32 CTileLightGrid::GetSliceDepth( int32 Boundary ) const
{
	if ( !IsValid() || Boundary < 0 || Boundary > NumSlices )
	{
		return 0.0f;
	}
	return MakeSliceDepth( NearDepth, FarDepth, NumSlices, Boundary );
}

int32 CTileLightGrid::GetClusterIndex( int32 TileX, int32 TileY, int32 Slice ) const
{
	if ( TileX < 0 || TileY < 0 || Slice < 0 || TileX >= NumTilesX || TileY >= NumTilesY || Slice >= NumSlices )
	{
		return -1;
	}
	return ( Slice * NumTilesY + TileY ) * NumTilesX + TileX;
}

int32 CTileLightGrid::GetTileCount( int32 TileX, int32 TileY, int32 Slice ) const
{
	const int32 Cluster = GetClusterIndex( TileX, TileY, Slice );
	return Cluster < 0 ? 0 : Counts[Cluster];
}

int32 CTileLightGrid::GetTileLight( int32 TileX, int32 TileY, int32 Index, int32 Slice ) const
{
	if ( Index < 0 || Index >= GetTileCount( TileX, TileY, Slice ) )
	{
		return -1;
	}
	return Entries[GetClusterIndex( TileX, TileY, Slice ) * Desc.MaxLightsPerTile + Index];
}

bool CTileLightGrid::IsTileOverflowed( int32 TileX, int32 TileY, int32 Slice ) const
{
	const int32 Cluster = GetClusterIndex( TileX, TileY, Slice );
	return Cluster >= 0 && Overflowed[Cluster] != 0;
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
	for ( int32 Slice = 0; Slice < NumSlices; ++Slice )
	{
		for ( int32 TileY = 0; TileY < NumTilesY; ++TileY )
		{
			for ( int32 TileX = 0; TileX < NumTilesX; ++TileX )
			{
				const bool bOverflowed = IsTileOverflowed( TileX, TileY, Slice );
				const int32 Count = GetTileCount( TileX, TileY, Slice );
				const int32 Row = Slice * NumTilesY + TileY;
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
								Components[Component] = static_cast<uint8>( GetTileLight( TileX, TileY, Index, Slice ) + 1 );
							}
						}
					}
					Out.Set( TileX * TexelsPerTile + Texel, Row, FColor32( Components[0], Components[1], Components[2], 255 ) );
				}
			}
		}
	}
	return true;
}
