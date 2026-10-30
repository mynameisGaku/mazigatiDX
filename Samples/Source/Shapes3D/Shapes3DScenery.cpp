#include "Shapes3DScenery.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Render/Vertex3D.h"
#include "Source/Common/SoftLighting.h"

namespace
{
	// 床の大きさ（半分）と、市松模様の 1 周期（ワールドの単位。画像の 1 周期 = 2 x 2 マス）
	constexpr float32 FloorHalfWidth = 22.0f;
	constexpr float32 FloorHalfDepth = 14.0f;
	constexpr float32 FloorTileSize = 2.0f;

	// 星と蛍の数
	constexpr int32 NumStars = 220;
	constexpr int32 NumFireflies = 90;

	// 草の数と大きさ
	constexpr int32 NumGrassTufts = 14;
	constexpr float32 GrassSize = 1.8f;

	// 蛍の見た目の色と、星の遠さ
	const FColor32 FireflyColor( 255, 225, 130 );
	constexpr float32 StarDistance = 60.0f;

	// 見どころの位置
	const FVector3 GemCenter( -2.0f, 3.0f, -3.0f );
	const FVector3 HelixCenter( -9.0f, 0.0f, -4.0f );
	const FVector3 UmbrellaCenter( 9.0f, 2.8f, 3.0f );
	const FVector3 FlagPole( -3.0f, 5.0f, 6.0f );
	const FVector3 StarSpritePosition( 0.0f, 8.0f, 0.0f );
	const FVector3 HeartSpritePosition( 5.0f, 8.0f, 1.0f );
	const FVector3 LanternHook( 10.0f, 8.5f, -3.0f );

	// 0..1 の疑似乱数（番号から決まる）
	float32 Hash01( uint32 Seed )
	{
		Seed = Seed * 747796405u + 2891336453u;
		Seed = ( ( Seed >> ( ( Seed >> 28u ) + 4u ) ) ^ Seed ) * 277803737u;
		Seed = ( Seed >> 22u ) ^ Seed;
		return static_cast<float32>( Seed & 0xFFFFu ) / 65535.0f;
	}

	// 頂点（向きと色つき）。画像の座標は、あるときだけ決める
	FVertex3D MakeVertex( const FVector3& Position, const FVector3& Normal, const FColor32& Color, float32 U = 0.0f, float32 V = 0.0f )
	{
		return FVertex3D::Make( Position, Normal, Color, U, V );
	}

	// 面（ワールドの XZ の平面）: 市松模様の画像を、全体に繰り返し貼る。頂点の並びの「帯」で 1 枚の四角形にする（Primitive3D）
	void RecordFloor( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		if ( !State.FloorTexture.IsValid() )
		{
			return;
		}
		const float32 U = FloorHalfWidth * 2.0f / FloorTileSize;
		const float32 V = FloorHalfDepth * 2.0f / FloorTileSize;
		const FVector3 Up( 0.0f, 1.0f, 0.0f );
		const FColor32 White( 255, 255, 255 );
		const FVertex3D Strip[4] = {
			MakeVertex( FVector3( -FloorHalfWidth, 0.0f, -FloorHalfDepth ), Up, White, 0.0f, 0.0f ),
			MakeVertex( FVector3( -FloorHalfWidth, 0.0f, FloorHalfDepth ), Up, White, 0.0f, V ),
			MakeVertex( FVector3( FloorHalfWidth, 0.0f, -FloorHalfDepth ), Up, White, U, 0.0f ),
			MakeVertex( FVector3( FloorHalfWidth, 0.0f, FloorHalfDepth ), Up, White, U, V ),
		};
		CommandList.SurfaceMaterial( FSurfaceMaterial::MakeMatte() );
		CommandList.TextureAddress( EDrawTextureAddress::Wrap );
		CommandList.Primitive3D( Strip, 4, EDrawPrimitive::TriangleStrip, State.FloorTexture );
	}

	// 松の木（円錐 2 つと幹）。1 キーで、線で描く
	void RecordPines( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		const bool bFill = !State.bWireCones;
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			const float32 X = -15.0f + static_cast<float32>( Index ) * 2.7f;
			const float32 Z = 6.5f + static_cast<float32>( Index % 2 ) * 1.4f;
			CommandList.Cube3D( FVector3( X - 0.25f, 0.0f, Z - 0.25f ), FVector3( X + 0.25f, 1.6f, Z + 0.25f ), FColor32( 110, 72, 44 ) );
			CommandList.Cone3D( FVector3( X, 4.3f, Z ), FVector3( X, 1.2f, Z ), 1.8f, 24, FColor32( 22, 84, 44 ), FColor32::Black, bFill );
			CommandList.Cone3D( FVector3( X, 6.2f, Z ), FVector3( X, 3.2f, Z ), 1.3f, 24, FColor32( 30, 108, 56 ), FColor32::Black, bFill );
		}
	}

	// 星空（Pixel3D。1 画素の点）。フォグを受けない
	void RecordStars( FDrawContext& CommandList )
	{
		for ( int32 Index = 0; Index < NumStars; ++Index )
		{
			const float32 Azimuth = Hash01( static_cast<uint32>( Index ) * 2u ) * NMath::PI * 2.0f;
			const float32 Height = 0.18f + 0.82f * Hash01( static_cast<uint32>( Index ) * 2u + 1u );
			const float32 Ring = NMath::Sqrt( 1.0f - Height * Height );
			const uint8 Level = static_cast<uint8>( 150.0f + 105.0f * Hash01( static_cast<uint32>( Index ) + 1000u ) );
			CommandList.Pixel3D( FVector3( NMath::Cos( Azimuth ) * Ring, Height, NMath::Sin( Azimuth ) * Ring ) * StarDistance, FColor32( Level, Level, 255 ) );
		}
	}

	// 蛍（頂点の並びの点）。ゆっくり漂う。フォグを受ける
	void RecordFireflies( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		TArray<FVertex3D> Fireflies;
		for ( int32 Index = 0; Index < NumFireflies; ++Index )
		{
			const uint32 Seed = static_cast<uint32>( Index ) * 3u;
			const float32 Phase = static_cast<float32>( Index );
			const FVector3 Position(
				-12.0f + 24.0f * Hash01( Seed ) + NMath::Sin( State.Time * 0.5f + Phase ) * 0.8f,
				0.8f + 4.2f * Hash01( Seed + 1u ) + NMath::Sin( State.Time * 0.9f + Phase * 1.3f ) * 0.4f,
				-9.0f + 18.0f * Hash01( Seed + 2u ) );
			Fireflies.Add( MakeVertex( Position, FVector3( 0.0f, 1.0f, 0.0f ), FireflyColor ) );
		}
		CommandList.Primitive3D( Fireflies.GetData(), Fireflies.Num(), EDrawPrimitive::Points );
	}

	// 座標軸（頂点の並びの線）と、らせん階段の手すり（折れ線）。頂点の並びの線は、点と同じに、法線で光を受け、フォグも受ける（DrawLine3D と違う）。色をそのまま見せるために、光は切る
	void RecordLines( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		CommandList.UseLighting( false );
		const FVector3 Up( 0.0f, 1.0f, 0.0f );
		const FVertex3D Axes[6] = {
			MakeVertex( FVector3( 0.0f, 0.05f, 0.0f ), Up, FColor32( 255, 80, 80 ) ), MakeVertex( FVector3( 4.0f, 0.05f, 0.0f ), Up, FColor32( 255, 80, 80 ) ),
			MakeVertex( FVector3( 0.0f, 0.05f, 0.0f ), Up, FColor32( 80, 255, 80 ) ), MakeVertex( FVector3( 0.0f, 4.0f, 0.0f ), Up, FColor32( 80, 255, 80 ) ),
			MakeVertex( FVector3( 0.0f, 0.05f, 0.0f ), Up, FColor32( 90, 140, 255 ) ), MakeVertex( FVector3( 0.0f, 0.05f, 4.0f ), Up, FColor32( 90, 140, 255 ) ),
		};
		CommandList.Primitive3D( Axes, 6, EDrawPrimitive::Lines );

		constexpr int32 NumPoints = 97;
		TArray<FVertex3D> Helix;
		for ( int32 Index = 0; Index < NumPoints; ++Index )
		{
			const float32 Ratio = static_cast<float32>( Index ) / static_cast<float32>( NumPoints - 1 );
			const float32 Angle = Ratio * NMath::PI * 6.0f;
			const FVector3 Position = HelixCenter + FVector3( NMath::Cos( Angle ) * 2.0f, Ratio * 6.0f + 0.1f, NMath::Sin( Angle ) * 2.0f );
			Helix.Add( MakeVertex( Position, Up, FColor32( static_cast<uint8>( 255.0f * ( 1.0f - Ratio ) ), static_cast<uint8>( 140.0f + 100.0f * Ratio ), 255 ) ) );
		}
		CommandList.Primitive3D( Helix.GetData(), Helix.Num(), EDrawPrimitive::LineStrip );
		CommandList.UseLighting( State.bLighting );
	}

	// 宝石（八面体）。6 つの頂点と 24 個の添字で、8 つの三角形（PrimitiveIndexed3D）。頂点の法線は、中心から外へ向かう向き。回る
	void RecordGem( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		const float32 Radius = 1.4f;
		const FVector3 Directions[6] = { FVector3( 1.0f, 0.0f, 0.0f ), FVector3( -1.0f, 0.0f, 0.0f ), FVector3( 0.0f, 1.0f, 0.0f ), FVector3( 0.0f, -1.0f, 0.0f ), FVector3( 0.0f, 0.0f, 1.0f ), FVector3( 0.0f, 0.0f, -1.0f ) };
		const FColor32 Colors[6] = { FColor32( 255, 90, 120 ), FColor32( 90, 255, 200 ), FColor32( 255, 255, 140 ), FColor32( 120, 120, 255 ), FColor32( 255, 160, 60 ), FColor32( 170, 90, 255 ) };
		TArray<FVertex3D> Vertices;
		for ( int32 Index = 0; Index < 6; ++Index )
		{
			Vertices.Add( MakeVertex( Directions[Index] * Radius, Directions[Index], Colors[Index] ) );
		}
		// 8 つの面: 軸ごとに、+ 側と - 側から 1 つずつ選んだ組。表（画面で時計回り）が外を向くように、並びを決める
		TArray<uint16> Indices;
		for ( int32 SignX = 0; SignX < 2; ++SignX )
		{
			for ( int32 SignY = 0; SignY < 2; ++SignY )
			{
				for ( int32 SignZ = 0; SignZ < 2; ++SignZ )
				{
					const uint16 A = static_cast<uint16>( SignX );
					const uint16 B = static_cast<uint16>( 2 + SignY );
					const uint16 C = static_cast<uint16>( 4 + SignZ );
					const FVector3 Edge1 = Vertices[B].Position - Vertices[A].Position;
					const FVector3 Edge2 = Vertices[C].Position - Vertices[A].Position;
					const FVector3 Outward = Vertices[A].Position + Vertices[B].Position + Vertices[C].Position;
					// 辺の外積が外を向いていれば、A, B, C が表の並び（DxLib は左手系: 表の三角形は、外積が見る側を向く）
					const bool bOutward = FVector3::Dot( FVector3::Cross( Edge1, Edge2 ), Outward ) > 0.0f;
					Indices.Add( A );
					Indices.Add( bOutward ? B : C );
					Indices.Add( bOutward ? C : B );
				}
			}
		}
		const FMatrix4x4 Place = FMatrix4x4::RotateY( State.Time * 0.9f ) * FMatrix4x4::Translate( GemCenter + FVector3( 0.0f, NMath::Sin( State.Time * 1.4f ) * 0.3f, 0.0f ) );
		FWorldMatrixScope Scope( CommandList, Place );
		CommandList.PrimitiveIndexed3D( Vertices.GetData(), Vertices.Num(), Indices.GetData(), Indices.Num(), EDrawPrimitive::Triangles );
	}

	// 波（頂点の並びの帯）。奥と手前の 2 つの頂点を、手前から奥へ並べる。法線は、波の傾きから決める
	void RecordWave( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		constexpr int32 NumSteps = 40;
		const float32 Left = 1.0f;
		const float32 Width = 12.0f;
		const float32 Near = -9.5f;
		const float32 Far = -6.5f;
		TArray<FVertex3D> Strip;
		for ( int32 Index = 0; Index <= NumSteps; ++Index )
		{
			const float32 Ratio = static_cast<float32>( Index ) / static_cast<float32>( NumSteps );
			const float32 X = Left + Width * Ratio;
			const float32 Phase = Ratio * NMath::PI * 4.0f + State.Time * 2.0f;
			const float32 Y = 0.9f + NMath::Sin( Phase ) * 0.45f;
			// 傾き dY/dX の法線（X 方向にだけ傾く）
			const float32 Slope = NMath::Cos( Phase ) * 0.45f * NMath::PI * 4.0f / Width;
			const FVector3 Normal = FVector3( -Slope, 1.0f, 0.0f ).Normalized();
			const FColor32 Color( static_cast<uint8>( 40.0f + 60.0f * Ratio ), static_cast<uint8>( 120.0f + 110.0f * Ratio ), 255 );
			Strip.Add( MakeVertex( FVector3( X, Y, Near ), Normal, Color ) );
			Strip.Add( MakeVertex( FVector3( X, Y, Far ), Normal, Color ) );
		}
		FSurfaceMaterial Water = FSurfaceMaterial::MakeMatte();
		Water.Shininess = 60.0f;
		CommandList.SurfaceMaterial( Water );
		CommandList.Primitive3D( Strip.GetData(), Strip.Num(), EDrawPrimitive::TriangleStrip );
	}

	// 傘（頂点の並びの扇）。中心を高くして、縁を 24 個の頂点で回る。上から見て、表が上を向くように、縁は時計回りに並べる
	void RecordUmbrella( FDrawContext& CommandList )
	{
		constexpr int32 NumRim = 24;
		const float32 Radius = 2.6f;
		const float32 Drop = 1.1f;
		TArray<FVertex3D> Fan;
		Fan.Add( MakeVertex( UmbrellaCenter + FVector3( 0.0f, Drop, 0.0f ), FVector3( 0.0f, 1.0f, 0.0f ), FColor32( 255, 255, 255 ) ) );
		for ( int32 Index = 0; Index <= NumRim; ++Index )
		{
			const float32 Angle = -static_cast<float32>( Index ) * NMath::PI * 2.0f / static_cast<float32>( NumRim );
			const FVector3 Outward( NMath::Cos( Angle ), 0.0f, NMath::Sin( Angle ) );
			const FColor32 Color = Index % 2 == 0 ? FColor32( 230, 60, 70 ) : FColor32( 245, 245, 245 );
			Fan.Add( MakeVertex( UmbrellaCenter + Outward * Radius, ( Outward + FVector3( 0.0f, 0.7f, 0.0f ) ).Normalized(), Color ) );
		}
		CommandList.SurfaceMaterial( FSurfaceMaterial::MakeMatte() );
		CommandList.Primitive3D( Fan.GetData(), Fan.Num(), EDrawPrimitive::TriangleFan );
		// 傘の柄
		CommandList.Capsule3D( FVector3( UmbrellaCenter.x, 0.4f, UmbrellaCenter.z ), FVector3( UmbrellaCenter.x, UmbrellaCenter.y + Drop - 0.1f, UmbrellaCenter.z ), 0.12f, 12, FColor32( 120, 90, 60 ) );
	}

	// カメラへ向ける画像: 草・旗・回る星・脈打つ心臓・揺れるランタン。基準点の印（小さな赤い球）も置く。光は受けない
	void RecordBillboards( FDrawContext& CommandList, const FShapes3DDemoState& State )
	{
		if ( State.bTranslucent )
		{
			CommandList.Blend( EDrawBlend::Alpha, 150 );
		}

		// 草: 基準点が足元（0.5, 1）なら、地面に立つ。中心（0.5, 0.5）だと、半分が地面に埋まる
		if ( State.GrassTexture.IsValid() )
		{
			const float32 PivotY = State.bStandOnGround ? 1.0f : 0.5f;
			for ( int32 Index = 0; Index < NumGrassTufts; ++Index )
			{
				const float32 X = -7.0f + static_cast<float32>( Index ) * 1.1f;
				const float32 Z = -10.0f + Hash01( static_cast<uint32>( Index ) + 500u ) * 1.5f;
				CommandList.BillboardPivot3D( FVector3( X, 0.0f, Z ), GrassSize, State.GrassTexture, 0.5f, PivotY );
			}
		}

		// 旗: 4 つの角を動かす。ポールにつながる左の 2 つの角は動かず、右の 2 つの角が風で揺れる
		if ( State.FlagTexture.IsValid() )
		{
			const float32 Wave1 = NMath::Sin( State.Time * 3.0f ) * 0.25f;
			const float32 Wave2 = NMath::Sin( State.Time * 3.0f + 1.2f ) * 0.25f;
			CommandList.BillboardQuad3D( FlagPole, 0.0f, 0.0f, 2.8f + Wave1, Wave2 * 0.6f, 2.8f + Wave2, -1.4f + Wave1 * 0.5f, 0.0f, -1.4f, State.FlagTexture );
		}

		// 回る星（Sprite3D）: 画像の 1 ピクセルが 0.12 のワールドの長さ
		if ( State.StarTexture.IsValid() )
		{
			CommandList.Sprite3D( StarSpritePosition, State.StarTexture, 0.12f, State.Time * 1.2f );
		}

		// 脈打つ心臓（SpriteStretch3D）: 横と縦を、ずらした周期で伸び縮みさせる
		if ( State.HeartTexture.IsValid() )
		{
			CommandList.SpriteStretch3D( HeartSpritePosition, State.HeartTexture, 0.1f * ( 1.0f + 0.25f * NMath::Sin( State.Time * 3.0f ) ), 0.1f * ( 1.0f + 0.25f * NMath::Cos( State.Time * 3.0f ) ) );
		}

		// 揺れるランタン（SpritePivot3D）: 基準点を、画像の上の中心（ロープの根元）にして、そこを軸に振り子のように揺れる
		if ( State.LanternTexture.IsValid() )
		{
			CommandList.SpritePivot3D( LanternHook, State.LanternTexture, 12.0f, 1.0f, 0.075f, 0.075f, NMath::Sin( State.Time * 2.0f ) * 0.5f );
		}

		if ( State.bTranslucent )
		{
			CommandList.Blend( EDrawBlend::Opaque );
		}

		// 基準点の印（光の計算を切った、小さな赤い球）
		CommandList.UseLighting( false );
		const float32 MarkerRadius = 0.08f;
		for ( int32 Index = 0; Index < NumGrassTufts; Index += 3 )
		{
			const float32 X = -7.0f + static_cast<float32>( Index ) * 1.1f;
			const float32 Z = -10.0f + Hash01( static_cast<uint32>( Index ) + 500u ) * 1.5f;
			CommandList.Sphere3D( FVector3( X, 0.0f, Z ), MarkerRadius, 8, FColor32( 255, 60, 60 ) );
		}
		CommandList.Sphere3D( FlagPole, MarkerRadius, 8, FColor32( 255, 60, 60 ) );
		CommandList.Sphere3D( StarSpritePosition, MarkerRadius, 8, FColor32( 255, 60, 60 ) );
		CommandList.Sphere3D( HeartSpritePosition, MarkerRadius, 8, FColor32( 255, 60, 60 ) );
		CommandList.Sphere3D( LanternHook, MarkerRadius, 8, FColor32( 255, 60, 60 ) );
		CommandList.UseLighting( State.bLighting );
	}
}

bool NShapes3DScenery::MakeTextures( CAssetManager& Assets, FShapes3DDemoState& State )
{
	// 床: 8 x 8 画素の濃淡の市松模様（1 周期 = 2 x 2 マス）
	FPixelImage Checker( 8, 8 );
	for ( int32 Y = 0; Y < 8; ++Y )
	{
		for ( int32 X = 0; X < 8; ++X )
		{
			const bool bLight = ( ( X / 4 + Y / 4 ) % 2 ) == 0;
			Checker.Set( X, Y, bLight ? FColor32( 120, 130, 120 ) : FColor32( 70, 82, 72 ) );
		}
	}
	State.FloorTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Floor", Checker );

	// 草: 32 x 32 画素。下の中心が根元。細い葉が何本か立つ（背景は透明）
	FPixelImage Grass( 32, 32 );
	for ( int32 Y = 0; Y < 32; ++Y )
	{
		for ( int32 X = 0; X < 32; ++X )
		{
			const float32 Phase = static_cast<float32>( X % 5 ) - 2.0f;
			const float32 BladeHeight = 8.0f + 22.0f * Hash01( static_cast<uint32>( X / 5 ) + 7u ) * ( 1.0f - NMath::Abs( Phase ) / 3.0f );
			const bool bInside = static_cast<float32>( 31 - Y ) < BladeHeight;
			const uint8 Shade = static_cast<uint8>( 90 + 130 * Y / 31 );
			Grass.Set( X, Y, bInside ? FColor32( static_cast<uint8>( Shade / 3 ), Shade, static_cast<uint8>( Shade / 4 ), 255 ) : FColor32( 0, 0, 0, 0 ) );
		}
	}
	State.GrassTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Grass", Grass );

	// 旗: 64 x 32 画素。赤と白の横じま、左上に青い四角
	FPixelImage Flag( 64, 32 );
	for ( int32 Y = 0; Y < 32; ++Y )
	{
		for ( int32 X = 0; X < 64; ++X )
		{
			const bool bCanton = X < 26 && Y < 16;
			const bool bRed = ( Y / 4 ) % 2 == 0;
			Flag.Set( X, Y, bCanton ? FColor32( 40, 60, 160 ) : ( bRed ? FColor32( 210, 50, 60 ) : FColor32( 240, 240, 240 ) ) );
		}
	}
	State.FlagTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Flag", Flag );

	// 星: 32 x 32 画素。5 つの角がある、金色の星（背景は透明）
	FPixelImage Star( 32, 32 );
	for ( int32 Y = 0; Y < 32; ++Y )
	{
		for ( int32 X = 0; X < 32; ++X )
		{
			const float32 OffsetX = static_cast<float32>( X ) - 15.5f;
			const float32 OffsetY = 15.5f - static_cast<float32>( Y );
			const float32 Distance = NMath::Sqrt( OffsetX * OffsetX + OffsetY * OffsetY );
			const float32 Angle = NMath::Atan2( OffsetY, OffsetX ) - NMath::PI * 0.5f;
			const float32 Reach = 15.5f * ( 0.42f + 0.58f * NMath::Abs( NMath::Cos( 2.5f * Angle ) ) );
			const uint8 Shade = static_cast<uint8>( 255.0f - 90.0f * Distance / 15.5f );
			Star.Set( X, Y, Distance < Reach ? FColor32( Shade, static_cast<uint8>( Shade * 0.82f ), 40, 255 ) : FColor32( 0, 0, 0, 0 ) );
		}
	}
	State.StarTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Star", Star );

	// 心臓: 32 x 32 画素。赤いハート（背景は透明）
	FPixelImage Heart( 32, 32 );
	for ( int32 Y = 0; Y < 32; ++Y )
	{
		for ( int32 X = 0; X < 32; ++X )
		{
			const float32 U = ( static_cast<float32>( X ) - 15.5f ) / 13.5f;
			const float32 V = ( 14.5f - static_cast<float32>( Y ) ) / 13.5f;
			const float32 Sum = U * U + V * V - 1.0f;
			const bool bInside = Sum * Sum * Sum - U * U * V * V * V <= 0.0f;
			const uint8 Shade = static_cast<uint8>( 255.0f - 70.0f * static_cast<float32>( Y ) / 31.0f );
			Heart.Set( X, Y, bInside ? FColor32( Shade, 40, 70, 255 ) : FColor32( 0, 0, 0, 0 ) );
		}
	}
	State.HeartTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Heart", Heart );

	// ランタン: 24 x 40 画素。上の中心の細いロープと、橙色に光る本体（背景は透明）
	FPixelImage Lantern( 24, 40 );
	for ( int32 Y = 0; Y < 40; ++Y )
	{
		for ( int32 X = 0; X < 24; ++X )
		{
			const bool bRope = Y < 10 && X >= 11 && X <= 12;
			const bool bBody = Y >= 10 && Y < 38 && X >= 3 && X < 21;
			const bool bFrame = bBody && ( X < 5 || X >= 19 || Y < 13 || Y >= 35 );
			FColor32 Color( 0, 0, 0, 0 );
			if ( bRope )
			{
				Color = FColor32( 90, 70, 50, 255 );
			}
			else if ( bFrame )
			{
				Color = FColor32( 70, 50, 30, 255 );
			}
			else if ( bBody )
			{
				const uint8 Glow = static_cast<uint8>( 255 - 70 * ( Y - 13 ) / 22 );
				Color = FColor32( Glow, static_cast<uint8>( Glow * 0.7f ), 60, 255 );
			}
			Lantern.Set( X, Y, Color );
		}
	}
	State.LanternTexture = Assets.CreateDynamicTexture( "Samples/Shapes3D/Lantern", Lantern );

	return State.FloorTexture.IsValid() && State.GrassTexture.IsValid() && State.FlagTexture.IsValid() && State.StarTexture.IsValid() && State.HeartTexture.IsValid() && State.LanternTexture.IsValid();
}

TArray<NShapes3DScenery::FStageLabel> NShapes3DScenery::MakeLabels( const FShapes3DDemoState& State )
{
	TArray<FStageLabel> Labels;
	Labels.Add( FStageLabel { "Cone3D (pines)", FVector3( -11.0f, 8.0f, 7.0f ) } );
	Labels.Add( FStageLabel { "Pixel3D (stars)", FVector3( 0.0f, 22.0f, 10.0f ) } );
	Labels.Add( FStageLabel { "Primitive3D Points (fireflies)", FVector3( 3.0f, 6.6f, -5.0f ) } );
	Labels.Add( FStageLabel { "Lines + LineStrip", HelixCenter + FVector3( 0.0f, 7.2f, 0.0f ) } );
	Labels.Add( FStageLabel { "PrimitiveIndexed3D (gem)", GemCenter + FVector3( 0.0f, 1.9f, 0.0f ) } );
	Labels.Add( FStageLabel { "TriangleStrip (wave)", FVector3( 7.0f, 2.6f, -8.0f ) } );
	Labels.Add( FStageLabel { "TriangleFan (umbrella)", UmbrellaCenter + FVector3( 0.0f, 2.6f, 0.0f ) } );
	Labels.Add( FStageLabel { State.bStandOnGround ? "BillboardPivot3D (pivot: feet)" : "BillboardPivot3D (pivot: center)", FVector3( -2.0f, 2.4f, -10.0f ) } );
	Labels.Add( FStageLabel { "BillboardQuad3D (flag)", FlagPole + FVector3( 1.4f, 1.0f, 0.0f ) } );
	Labels.Add( FStageLabel { "Sprite3D", StarSpritePosition + FVector3( 0.0f, 2.6f, 0.0f ) } );
	Labels.Add( FStageLabel { "SpriteStretch3D", HeartSpritePosition + FVector3( 0.0f, 2.6f, 0.0f ) } );
	Labels.Add( FStageLabel { "SpritePivot3D", LanternHook + FVector3( 0.0f, 1.2f, 0.0f ) } );
	return Labels;
}

void NShapes3DScenery::RecordStage( FDrawContext& CommandList, const FShapes3DDemoState& State )
{
	// 光の計算は、設定で入り切り。DxLib に最初からある 1 つのライトと、反対側の弱い補助光 + 光の回り込みで、光の当たらない側も真っ黒にしない
	CommandList.UseLighting( State.bLighting );
	FSoftLightingScope Soft( CommandList );

	// フォグ（2 キー）: 距離で、暗い青に溶ける。頂点の並び（点・線・三角形）には掛かるが、Pixel3D（星）には掛からない
	if ( State.bFog )
	{
		CommandList.Fog( FFogSettings::MakeLinear( FColor32( 14, 18, 36 ), 20.0f, 60.0f ) );
	}

	RecordFloor( CommandList, State );
	CommandList.SurfaceMaterial( FSurfaceMaterial::MakeMatte() );
	RecordPines( CommandList, State );
	RecordStars( CommandList );
	RecordFireflies( CommandList, State );
	RecordLines( CommandList, State );

	// 頂点の並びの多角形（3 キー）の Culling。床は、いつも両面を描く（上でもう描いた）
	switch ( State.CullingMode )
	{
	case 1:
		CommandList.Culling( EDrawCulling::BackFaces );
		break;
	case 2:
		CommandList.Culling( EDrawCulling::FrontFaces );
		break;
	default:
		break;
	}
	RecordGem( CommandList, State );
	RecordWave( CommandList, State );
	RecordUmbrella( CommandList );
	CommandList.Culling( EDrawCulling::None );

	RecordBillboards( CommandList, State );
}
