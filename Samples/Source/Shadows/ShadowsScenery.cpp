#include "ShadowsScenery.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Render/Vertex3D.h"
#include "Source/Common/SoftLighting.h"

namespace
{
	// 芝生の広さ（半分）と、市松模様の 1 周期（ワールドの単位。画像の 1 周期 = 2 x 2 マス）
	constexpr float32 GroundHalfSize = 32.0f;
	constexpr float32 GroundTileSize = 4.0f;

	// 太陽の高さ（水平からの角度。ラジアン。約 49 度）
	constexpr float32 SunElevation = 0.85f;

	// ランタンの位置（家の戸口の脇）
	const FVector3 LanternPosition( 7.0f, 1.5f, 6.0f );

	// 影を計算する範囲の広さ（半分）: 中庭だけ・広い範囲。高さは、家の屋根より上まで
	constexpr float32 CourtyardHalfSize = 22.0f;
	constexpr float32 WideHalfSize = 45.0f;
	constexpr float32 AreaTop = 14.0f;
	constexpr float32 AreaBottom = -1.0f;

	// 補正の深さの段階: 既定・0（床が自分の影で縞になる）・大きい（影が物から離れる）
	const float32 AdjustDepths[FShadowsDemoState::NumDepthModes] = { FShadowMapView::DefaultAdjustDepth, 0.0f, 0.05f };

	// 柵（すき間のある画像を貼った、立てた板）: 横の端と高さ、置く奥行き。表と裏の 2 枚を、わずかに離して置く
	constexpr float32 FenceLeft = -14.0f;
	constexpr float32 FenceRight = -6.0f;
	constexpr float32 FenceHeight = 2.4f;
	constexpr float32 FenceZ = 7.0f;
	constexpr float32 FenceThickness = 0.04f;

	// 色
	const FColor32 WallColor( 214, 196, 160 );
	const FColor32 RoofColor( 168, 70, 58 );
	const FColor32 TrunkColor( 110, 78, 48 );
	const FColor32 LeafColor( 46, 128, 66 );
	const FColor32 CrateColor( 176, 134, 82 );
	const FColor32 TravelerColor( 228, 92, 72 );

	// 木の根元の位置（x, z）
	const FVector2 TreePositions[] = {
		FVector2( -14.0f, -8.0f ), FVector2( -12.0f, -14.0f ), FVector2( 2.0f, -17.0f ), FVector2( 14.0f, -12.0f ),
		FVector2( 16.0f, 4.0f ), FVector2( -17.0f, 2.0f ), FVector2( -6.0f, 14.0f ), FVector2( 8.0f, 15.0f ),
	};

	// 旅人の位置: 家のまわりを円を描いて歩き、弾む
	FVector3 GetTravelerPosition( float32 Time )
	{
		const float32 Angle = Time * 0.45f;
		const float32 Bounce = NMath::Abs( NMath::Sin( Time * 3.0f ) ) * 0.6f;
		return FVector3( NMath::Cos( Angle ) * 10.0f, 0.9f + Bounce, NMath::Sin( Angle ) * 10.0f );
	}

	// 三角形を足す。法線は辺から計算して、Outward の側に向ける（面が閉じた形の外側を向くように）
	void AddTriangle( TArray<FVertex3D>& Vertices, const FVector3& A, const FVector3& B, const FVector3& C, const FVector3& Outward, const FColor32& Color )
	{
		FVector3 Normal = FVector3::Cross( B - A, C - A ).Normalized();
		if ( FVector3::Dot( Normal, Outward ) < 0.0f )
		{
			Normal = Normal * -1.0f;
		}
		Vertices.Add( FVertex3D::Make( A, Normal, Color ) );
		Vertices.Add( FVertex3D::Make( B, Normal, Color ) );
		Vertices.Add( FVertex3D::Make( C, Normal, Color ) );
	}

	// 立てた板の 1 枚（画像の左上が (Left, Height)、右下が (Right, 0)。法線は Normal）
	void AddFencePanel( TArray<FVertex3D>& Vertices, float32 Z, const FVector3& Normal )
	{
		const FColor32 White( 255, 255, 255 );
		const FVertex3D TopLeft = FVertex3D::Make( FVector3( FenceLeft, FenceHeight, Z ), Normal, White, 0.0f, 0.0f );
		const FVertex3D TopRight = FVertex3D::Make( FVector3( FenceRight, FenceHeight, Z ), Normal, White, 1.0f, 0.0f );
		const FVertex3D BottomRight = FVertex3D::Make( FVector3( FenceRight, 0.0f, Z ), Normal, White, 1.0f, 1.0f );
		const FVertex3D BottomLeft = FVertex3D::Make( FVector3( FenceLeft, 0.0f, Z ), Normal, White, 0.0f, 1.0f );
		Vertices.Add( TopLeft );
		Vertices.Add( TopRight );
		Vertices.Add( BottomRight );
		Vertices.Add( TopLeft );
		Vertices.Add( BottomRight );
		Vertices.Add( BottomLeft );
	}

	// 家: 壁（箱）・屋根（切妻。2 つの斜面と両端の三角形）・煙突
	void RecordHouse( FDrawContext& CommandList )
	{
		CommandList.Cube3D( FVector3( -4.0f, 0.0f, -3.0f ), FVector3( 4.0f, 5.0f, 3.0f ), WallColor );

		constexpr float32 EaveY = 5.0f;
		constexpr float32 RidgeY = 7.5f;
		constexpr float32 EaveZ = 3.8f;
		constexpr float32 HalfLength = 4.6f;
		const FVector3 RidgeLeft( -HalfLength, RidgeY, 0.0f );
		const FVector3 RidgeRight( HalfLength, RidgeY, 0.0f );
		const FVector3 FrontLeft( -HalfLength, EaveY, -EaveZ );
		const FVector3 FrontRight( HalfLength, EaveY, -EaveZ );
		const FVector3 BackLeft( -HalfLength, EaveY, EaveZ );
		const FVector3 BackRight( HalfLength, EaveY, EaveZ );
		const FVector3 FrontOutward( 0.0f, EaveZ, -( RidgeY - EaveY ) );
		const FVector3 BackOutward( 0.0f, EaveZ, RidgeY - EaveY );

		TArray<FVertex3D> Roof;
		AddTriangle( Roof, FrontLeft, FrontRight, RidgeRight, FrontOutward, RoofColor );
		AddTriangle( Roof, FrontLeft, RidgeRight, RidgeLeft, FrontOutward, RoofColor );
		AddTriangle( Roof, BackLeft, BackRight, RidgeRight, BackOutward, RoofColor );
		AddTriangle( Roof, BackLeft, RidgeRight, RidgeLeft, BackOutward, RoofColor );
		AddTriangle( Roof, FrontLeft, BackLeft, RidgeLeft, FVector3( -1.0f, 0.0f, 0.0f ), RoofColor );
		AddTriangle( Roof, FrontRight, BackRight, RidgeRight, FVector3( 1.0f, 0.0f, 0.0f ), RoofColor );
		CommandList.Primitive3D( Roof.GetData(), Roof.Num(), EDrawPrimitive::Triangles );

		CommandList.Cube3D( FVector3( 1.5f, 6.0f, -0.5f ), FVector3( 2.5f, 9.2f, 0.5f ), WallColor );
	}

	// 木: 幹（箱）と、二段の円錐の葉
	void RecordTrees( FDrawContext& CommandList )
	{
		for ( const FVector2& Position : TreePositions )
		{
			CommandList.Cube3D( FVector3( Position.x - 0.25f, 0.0f, Position.y - 0.25f ), FVector3( Position.x + 0.25f, 1.6f, Position.y + 0.25f ), TrunkColor );
			CommandList.Cone3D( FVector3( Position.x, 6.4f, Position.y ), FVector3( Position.x, 1.6f, Position.y ), 1.8f, 12, LeafColor );
			CommandList.Cone3D( FVector3( Position.x, 8.0f, Position.y ), FVector3( Position.x, 4.4f, Position.y ), 1.3f, 12, LeafColor );
		}
	}

	// 木箱 3 つ（家の脇）
	void RecordCrates( FDrawContext& CommandList )
	{
		CommandList.Cube3D( FVector3( -9.0f, 0.0f, -6.0f ), FVector3( -7.0f, 2.0f, -4.0f ), CrateColor );
		CommandList.Cube3D( FVector3( -7.0f, 0.0f, -6.0f ), FVector3( -5.5f, 1.4f, -4.5f ), CrateColor );
		CommandList.Cube3D( FVector3( -8.6f, 2.0f, -5.6f ), FVector3( -7.4f, 3.2f, -4.4f ), CrateColor );
	}

	// 旅人（球）
	void RecordTraveler( FDrawContext& CommandList, float32 Time )
	{
		CommandList.Sphere3D( GetTravelerPosition( Time ), 0.9f, 24, TravelerColor );
	}

	// 柵: すき間のある画像を貼った板。表と裏
	void RecordFence( FDrawContext& CommandList, const FShadowsDemoState& State )
	{
		if ( !State.FenceTexture.IsValid() )
		{
			return;
		}
		TArray<FVertex3D> Panels;
		AddFencePanel( Panels, FenceZ - FenceThickness, FVector3( 0.0f, 0.0f, -1.0f ) );
		AddFencePanel( Panels, FenceZ + FenceThickness, FVector3( 0.0f, 0.0f, 1.0f ) );
		CommandList.Primitive3D( Panels.GetData(), Panels.Num(), EDrawPrimitive::Triangles, State.FenceTexture );
	}

	// 影を落とすもの（影の絵にも、画面にも、同じものを描く）
	void RecordCasters( FDrawContext& CommandList, const FShadowsDemoState& State )
	{
		RecordHouse( CommandList );
		RecordTrees( CommandList );
		RecordCrates( CommandList );
		RecordTraveler( CommandList, State.Time );
		RecordFence( CommandList, State );
	}

	// 芝生（市松模様を貼った四角形。影を受けるだけで、影は落とさない）
	void RecordGround( FDrawContext& CommandList, const FShadowsDemoState& State )
	{
		const FColor32 White( 255, 255, 255 );
		const FVector3 Up( 0.0f, 1.0f, 0.0f );
		const float32 Edge = GroundHalfSize / GroundTileSize;
		const FVertex3D Corners[4] = {
			FVertex3D::Make( FVector3( -GroundHalfSize, 0.0f, -GroundHalfSize ), Up, White, -Edge, -Edge ),
			FVertex3D::Make( FVector3( GroundHalfSize, 0.0f, -GroundHalfSize ), Up, White, Edge, -Edge ),
			FVertex3D::Make( FVector3( GroundHalfSize, 0.0f, GroundHalfSize ), Up, White, Edge, Edge ),
			FVertex3D::Make( FVector3( -GroundHalfSize, 0.0f, GroundHalfSize ), Up, White, -Edge, Edge ),
		};
		const FVertex3D Vertices[6] = { Corners[0], Corners[1], Corners[2], Corners[0], Corners[2], Corners[3] };
		if ( State.FloorTexture.IsValid() )
		{
			CommandList.Primitive3D( Vertices, 6, EDrawPrimitive::Triangles, State.FloorTexture );
		}
		else
		{
			CommandList.Primitive3D( Vertices, 6, EDrawPrimitive::Triangles );
		}
	}
}

bool NShadowsScenery::MakeTextures( CAssetManager& Assets, FShadowsDemoState& State )
{
	// 芝生: 8 x 8 画素の濃淡の市松模様（1 周期 = 2 x 2 マス）
	FPixelImage Checker( 8, 8 );
	for ( int32 Y = 0; Y < 8; ++Y )
	{
		for ( int32 X = 0; X < 8; ++X )
		{
			const bool bLight = ( ( X / 4 + Y / 4 ) % 2 ) == 0;
			Checker.Set( X, Y, bLight ? FColor32( 104, 156, 82 ) : FColor32( 86, 136, 68 ) );
		}
	}
	State.FloorTexture = Assets.CreateDynamicTexture( "Samples/Shadows/Ground", Checker );

	// 柵: 64 x 16 画素。幅 6 画素の板が 8 画素おきに並び（先がとがる）、すき間は透明（アルファ 0）。横の桟は不透明
	FPixelImage Fence( 64, 16 );
	for ( int32 Y = 0; Y < 16; ++Y )
	{
		for ( int32 X = 0; X < 64; ++X )
		{
			const int32 Column = X % 8;
			const int32 Trim = Y == 0 ? 2 : ( Y < 3 ? 1 : 0 );
			const bool bPicket = Column >= Trim && Column < 6 - Trim;
			const bool bRail = ( Y >= 5 && Y < 7 ) || ( Y >= 12 && Y < 14 );
			Fence.Set( X, Y, bPicket || bRail ? FColor32( 200, 160, 108, 255 ) : FColor32( 0, 0, 0, 0 ) );
		}
	}
	State.FenceTexture = Assets.CreateDynamicTexture( "Samples/Shadows/Fence", Fence );

	return State.FloorTexture.IsValid() && State.FenceTexture.IsValid();
}

bool NShadowsScenery::UpdateShadowMap( FShadowsDemoState& State )
{
	if ( State.ShadowMap.IsValid() && State.ShadowMapSizeLevel == State.SizeLevel )
	{
		return true;
	}
	const int32 Level = NMath::Clamp( State.SizeLevel, 0, FShadowsDemoState::NumSizeLevels - 1 );
	const int32 Size = FShadowsDemoState::MapSizes[Level];
	const TSharedPtr<CShadowMap> Map = CShadowMap::Create( Size, Size );
	if ( !Map.IsValid() )
	{
		// 作れなければ、前のものを使い続ける
		return false;
	}
	State.ShadowMap = Map;
	State.ShadowMapSizeLevel = State.SizeLevel;
	return true;
}

FVector3 NShadowsScenery::GetSunDirection( const FShadowsDemoState& State )
{
	const float32 Horizontal = NMath::Cos( SunElevation );
	return FVector3( Horizontal * NMath::Cos( State.SunAzimuth ), -NMath::Sin( SunElevation ), Horizontal * NMath::Sin( State.SunAzimuth ) );
}

FLight NShadowsScenery::MakeSun( const FShadowsDemoState& State )
{
	return FLight::MakeDirectional( GetSunDirection( State ), FColor( 1.0f, 0.95f, 0.85f ) );
}

FLight NShadowsScenery::MakeSkyLight()
{
	// 真上からの、青みがかった弱い光（空の明かり）。影の中の地面や屋根を、うっすら照らす。影の絵の影は受けない
	FLight Sky = FLight::MakeDirectional( FVector3( 0.0f, -1.0f, 0.0f ), FColor( 0.30f, 0.36f, 0.48f ) );
	Sky.Specular = FColor::Black;
	Sky.ShadowMapSlots = 0;
	return Sky;
}

FLight NShadowsScenery::MakeLantern()
{
	FLight Lantern = FLight::MakePoint( LanternPosition, 10.0f, FColor( 1.0f, 0.62f, 0.28f ) );
	// 影の絵の影を受けない（太陽の影の中も、ランタンは照らす）
	Lantern.ShadowMapSlots = 0;
	return Lantern;
}

FShadowMapView NShadowsScenery::MakeShadowView( const FShadowsDemoState& State )
{
	const FVector3 Direction = GetSunDirection( State );
	FShadowMapView View;
	if ( State.AreaMode == 2 )
	{
		View = FShadowMapView::MakeFitToCamera( Direction );
	}
	else
	{
		const float32 Half = State.AreaMode == 0 ? CourtyardHalfSize : WideHalfSize;
		View = FShadowMapView::MakeForArea( Direction, FVector3( -Half, AreaBottom, -Half ), FVector3( Half, AreaTop, Half ) );
	}
	View.AdjustDepth = AdjustDepths[NMath::Clamp( State.DepthMode, 0, FShadowsDemoState::NumDepthModes - 1 )];
	return View;
}

TArray<NShadowsScenery::FStageLabel> NShadowsScenery::MakeLabels( const FShadowsDemoState& State )
{
	TArray<FStageLabel> Labels;
	Labels.Add( FStageLabel { "House", FVector3( 0.0f, 10.0f, 0.0f ) } );
	Labels.Add( FStageLabel { "Fence (gaps in its shadow)", FVector3( ( FenceLeft + FenceRight ) * 0.5f, FenceHeight + 0.6f, FenceZ ) } );
	Labels.Add( FStageLabel { "Traveler", GetTravelerPosition( State.Time ) + FVector3( 0.0f, 1.6f, 0.0f ) } );
	if ( State.bLantern )
	{
		Labels.Add( FStageLabel { "Lantern (never shadowed)", LanternPosition + FVector3( 0.0f, 0.9f, 0.0f ) } );
	}
	return Labels;
}

void NShadowsScenery::RecordStage( FDrawContext& CommandList, const FShadowsDemoState& State )
{
	const bool bShadows = State.bShadows && State.ShadowMap.IsValid();

	// ① 影の絵: 影を落とすものを、太陽の向きから見た深度として、シャドウマップへ描く。太陽も旅人も動くので、毎フレーム描き直す
	if ( bShadows )
	{
		FShadowMapPassScope Pass( CommandList, State.ShadowMap, MakeShadowView( State ) );
		RecordCasters( CommandList, State );
	}

	// ② ライト: DxLib に最初からあるライトは切って、太陽（動く方向ライト）を足す。空の明かり・やわらかい光の補助光・ランタンは、影を受けない
	CommandList.DefaultLight( false );
	FLightScope Sun( CommandList, MakeSun( State ) );
	FLightScope Sky( CommandList, MakeSkyLight() );
	FSoftLightingScope Soft( CommandList );
	const bool bLanternPushed = State.bLantern && CommandList.PushLight( MakeLantern() );

	// ③ 影を受けるもの: 芝生と、影を落とすものと同じもの
	{
		FShadowMapUseScope Shadow( CommandList, 0, bShadows ? State.ShadowMap : TSharedPtr<CShadowMap>() );
		RecordGround( CommandList, State );
		RecordCasters( CommandList, State );
	}
	if ( bLanternPushed )
	{
		CommandList.PopLight();
	}

	// ランタンの玉: 光の計算を切って、自分で光って見せる
	if ( State.bLantern )
	{
		CommandList.UseLighting( false );
		CommandList.Sphere3D( LanternPosition, 0.28f, 16, FColor32( 255, 196, 96 ) );
		CommandList.UseLighting( true );
	}
}
