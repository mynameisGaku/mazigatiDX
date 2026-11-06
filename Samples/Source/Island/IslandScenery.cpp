#include "IslandScenery.h"

#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Framework/Render/State/ShadowMapView.h"
#include "Library/Framework/Render/Types/Vertex3D.h"
#include "Source/Common/SoftLighting.h"

namespace
{
	// 影の絵の大きさと、影を計算する範囲: カメラの見える範囲のうち、手前から ShadowDistance まで（後ろと遠くには影を出さない。範囲が狭いほど、輪郭が細かい）。
	// 斜めに見下ろすカメラでは、見える範囲を囲む箱（ワールドの軸に沿う）が横に広がる（距離 60 で約 100 x 80 = 島全体と同じくらい）ので、霧が始まる少し手前の 35 までにする（約 60 x 50）。
	// 高さは、海の底から灯台の上まで。横は、見える範囲のすぐ外の木の影も入るよう、少し広げる
	constexpr int32 ShadowMapSize = 2048;
	constexpr float32 ShadowDistance = 35.0f;
	constexpr float32 ShadowBottom = -3.0f;
	constexpr float32 ShadowTop = 16.0f;
	constexpr float32 ShadowMargin = 4.0f;

	// 海の広さ（片側）と色（アルファで透ける）
	constexpr float32 SeaHalfSize = 260.0f;
	const FColor32 SeaColor( 40, 104, 166, 176 );

	// 霧が始まる距離と、空の色に溶け切る距離
	constexpr float32 FogStart = 45.0f;
	constexpr float32 FogEnd = 150.0f;

	// 色
	const FColor32 TrunkColor( 104, 74, 46 );
	const FColor32 LeafColor( 46, 116, 60 );
	const FColor32 TowerWhite( 232, 228, 220 );
	const FColor32 TowerRed( 196, 64, 52 );
	const FColor32 ExplorerColor( 236, 120, 52 );
	const FColor32 SkinColor( 240, 204, 170 );
	const FColor32 HatColor( 120, 86, 52 );
	const FColor32 CrystalColor( 120, 230, 255 );
	const FColor32 CrystalGlowColor( 90, 200, 255 );
	const FColor32 LampColor( 255, 226, 140 );
	const FColor32 TargetColor( 255, 236, 120 );

	// 灯台の高さ（段の数と、1 段の高さ）
	constexpr int32 TowerSegments = 5;
	constexpr float32 SegmentHeight = 1.8f;

	// 結晶の大きさ（芯から先までの長さ）と、拾ったあとの輪が広がる時間（秒）
	constexpr float32 CrystalSize = 0.7f;
	constexpr float32 RingDuration = 0.8f;

	FColor32 LerpColor( const FColor32& A, const FColor32& B, float32 T )
	{
		return FColor32( static_cast<uint8>( NMath::Lerp( static_cast<float32>( A.r ), static_cast<float32>( B.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( A.g ), static_cast<float32>( B.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( A.b ), static_cast<float32>( B.b ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( A.a ), static_cast<float32>( B.a ), T ) ) );
	}

	// 太陽の高さ（水平からの角度。ラジアン）。朝と夕方は低く、昼に高い
	float32 GetSunElevation( const FIslandState& State )
	{
		return 0.18f + 0.95f * NMath::Sin( NMath::PI * State.DayTime );
	}

	// 灯台の灯の位置
	FVector3 GetLampPosition( const FIslandState& State )
	{
		return State.LighthousePosition + FVector3( 0.0f, TowerSegments * SegmentHeight + 1.0f, 0.0f );
	}

	// 木: 幹（箱）と、二段の円錐の葉
	void RecordTrees( FDrawContext& CommandList, const FIslandState& State )
	{
		for ( const FIslandState::FTree& Tree : State.Trees )
		{
			const FVector3& Root = Tree.Position;
			const float32 S = Tree.Scale;
			CommandList.Cube3D( Root + FVector3( -0.22f * S, -0.3f, -0.22f * S ), Root + FVector3( 0.22f * S, 1.4f * S, 0.22f * S ), TrunkColor );
			CommandList.Cone3D( Root + FVector3( 0.0f, 5.2f * S, 0.0f ), Root + FVector3( 0.0f, 1.2f * S, 0.0f ), 1.6f * S, 10, LeafColor );
			CommandList.Cone3D( Root + FVector3( 0.0f, 6.6f * S, 0.0f ), Root + FVector3( 0.0f, 3.6f * S, 0.0f ), 1.15f * S, 10, LeafColor );
		}
	}

	// 灯台: 赤と白の段・灯室・屋根
	void RecordLighthouse( FDrawContext& CommandList, const FIslandState& State )
	{
		const FVector3& Base = State.LighthousePosition;
		for ( int32 Segment = 0; Segment < TowerSegments; ++Segment )
		{
			const float32 Half = 1.5f - 0.18f * static_cast<float32>( Segment );
			const float32 Bottom = static_cast<float32>( Segment ) * SegmentHeight - ( Segment == 0 ? 1.0f : 0.0f );
			const float32 Top = static_cast<float32>( Segment + 1 ) * SegmentHeight;
			CommandList.Cube3D( Base + FVector3( -Half, Bottom, -Half ), Base + FVector3( Half, Top, Half ), ( Segment % 2 ) == 0 ? TowerWhite : TowerRed );
		}
		const float32 RoomBottom = TowerSegments * SegmentHeight;
		CommandList.Cube3D( Base + FVector3( -0.8f, RoomBottom + 1.6f, -0.8f ), Base + FVector3( 0.8f, RoomBottom + 1.9f, 0.8f ), TowerRed );
		CommandList.Cone3D( Base + FVector3( 0.0f, RoomBottom + 3.2f, 0.0f ), Base + FVector3( 0.0f, RoomBottom + 1.9f, 0.0f ), 1.1f, 12, TowerRed );
	}

	// 探検家: 体（カプセル）・頭・帽子・向きを示す鼻。歩くと弾む
	void RecordExplorer( FDrawContext& CommandList, const FIslandState& State )
	{
		const float32 Bounce = NMath::Abs( NMath::Sin( State.Time * 9.0f ) ) * 0.12f * NMath::Clamp( State.ExplorerSpeed / 6.0f, 0.0f, 1.0f );
		const FVector3 Feet = State.ExplorerPosition + FVector3( 0.0f, Bounce, 0.0f );
		const FVector3 Forward( NMath::Sin( State.ExplorerYaw ), 0.0f, NMath::Cos( State.ExplorerYaw ) );
		CommandList.Capsule3D( Feet + FVector3( 0.0f, 0.45f, 0.0f ), Feet + FVector3( 0.0f, 1.15f, 0.0f ), 0.42f, 12, ExplorerColor );
		CommandList.Sphere3D( Feet + FVector3( 0.0f, 1.85f, 0.0f ), 0.34f, 16, SkinColor );
		CommandList.Cone3D( Feet + FVector3( 0.0f, 2.55f, 0.0f ), Feet + FVector3( 0.0f, 2.05f, 0.0f ), 0.5f, 12, HatColor );
		CommandList.Cone3D( Feet + FVector3( 0.0f, 1.85f, 0.0f ) + Forward * 0.55f, Feet + FVector3( 0.0f, 1.85f, 0.0f ) + Forward * 0.25f, 0.08f, 8, SkinColor );
	}

	// 地形（頂点バッファ + インデックスバッファ）
	void RecordTerrain( FDrawContext& CommandList, const FIslandState& State )
	{
		if ( State.TerrainVertices.IsValid() && State.TerrainIndices.IsValid() )
		{
			CommandList.PrimitiveIndexedBuffer3D( State.TerrainVertices, State.TerrainIndices, EDrawPrimitive::Triangles );
		}
	}

	// 影を落とすもの（影の絵にも、画面にも、同じものを描く）
	void RecordCasters( FDrawContext& CommandList, const FIslandState& State )
	{
		RecordTerrain( CommandList, State );
		RecordTrees( CommandList, State );
		RecordLighthouse( CommandList, State );
		RecordExplorer( CommandList, State );
	}

	// 海（半透明の大きな四角形。両面）
	void RecordSea( FDrawContext& CommandList )
	{
		const FVector3 Up( 0.0f, 1.0f, 0.0f );
		const float32 Y = FIslandState::SeaLevel;
		const FVertex3D Corners[4] = {
			FVertex3D::Make( FVector3( -SeaHalfSize, Y, -SeaHalfSize ), Up, SeaColor ),
			FVertex3D::Make( FVector3( SeaHalfSize, Y, -SeaHalfSize ), Up, SeaColor ),
			FVertex3D::Make( FVector3( SeaHalfSize, Y, SeaHalfSize ), Up, SeaColor ),
			FVertex3D::Make( FVector3( -SeaHalfSize, Y, SeaHalfSize ), Up, SeaColor ),
		};
		const FVertex3D Vertices[6] = { Corners[0], Corners[2], Corners[1], Corners[0], Corners[3], Corners[2] };
		CommandList.Blend( EDrawBlend::Alpha, 255 );
		CommandList.Primitive3D( Vertices, 6, EDrawPrimitive::Triangles );
		CommandList.Blend( EDrawBlend::Opaque );
	}

	// 結晶 1 つ（芯 Center を中心に、Angle だけ回した八面体）
	void RecordCrystalShape( FDrawContext& CommandList, const FVector3& Center, float32 Angle )
	{
		const FVector3 Top = Center + FVector3( 0.0f, CrystalSize * 1.4f, 0.0f );
		const FVector3 Bottom = Center - FVector3( 0.0f, CrystalSize * 1.4f, 0.0f );
		FVector3 Ring[4];
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			const float32 A = Angle + static_cast<float32>( Index ) * NMath::PI * 0.5f;
			Ring[Index] = Center + FVector3( NMath::Cos( A ) * CrystalSize, 0.0f, NMath::Sin( A ) * CrystalSize );
		}
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			const FVector3& A = Ring[Index];
			const FVector3& B = Ring[( Index + 1 ) % 4];
			// 面ごとに明るさを変えて、形が見えるようにする
			const float32 Shade = 0.7f + 0.3f * static_cast<float32>( Index % 2 );
			const FColor32 Upper( static_cast<uint8>( CrystalColor.r * Shade ), static_cast<uint8>( CrystalColor.g * Shade ), static_cast<uint8>( CrystalColor.b * Shade ) );
			const FColor32 Lower( static_cast<uint8>( CrystalColor.r * Shade * 0.7f ), static_cast<uint8>( CrystalColor.g * Shade * 0.7f ), static_cast<uint8>( CrystalColor.b * Shade * 0.8f ) );
			CommandList.Triangle3D( Top, A, B, Upper );
			CommandList.Triangle3D( Bottom, B, A, Lower );
		}
	}

	// 結晶: 光の計算を切って、自分で光って見せる。まわりに、加算で光のにじみを重ねる。拾ったものは、広がる輪を少しだけ見せる
	void RecordCrystals( FDrawContext& CommandList, const FIslandState& State )
	{
		CommandList.UseLighting( false );
		for ( int32 Index = 0; Index < State.Crystals.Num(); ++Index )
		{
			const FIslandState::FCrystal& Crystal = State.Crystals[Index];
			if ( !Crystal.bTaken )
			{
				RecordCrystalShape( CommandList, NIslandScenery::GetCrystalPosition( State, Index ), State.Time * 1.6f + static_cast<float32>( Index ) );
			}
		}
		if ( State.GlowTexture.IsValid() )
		{
			CommandList.Blend( EDrawBlend::Add, 200 );
			for ( int32 Index = 0; Index < State.Crystals.Num(); ++Index )
			{
				const FIslandState::FCrystal& Crystal = State.Crystals[Index];
				if ( !Crystal.bTaken )
				{
					const float32 Pulse = 3.6f + 0.6f * NMath::Sin( State.Time * 3.0f + static_cast<float32>( Index ) );
					CommandList.BillboardPivot3D( NIslandScenery::GetCrystalPosition( State, Index ), Pulse, State.GlowTexture );
				}
				else if ( State.Time - Crystal.TakenTime < RingDuration )
				{
					// 拾った瞬間: にじみが大きく広がって消える
					const float32 T = ( State.Time - Crystal.TakenTime ) / RingDuration;
					CommandList.Blend( EDrawBlend::Add, static_cast<int32>( 255.0f * ( 1.0f - T ) ) );
					CommandList.BillboardPivot3D( Crystal.Position + FVector3( 0.0f, 1.6f, 0.0f ), 3.0f + T * 9.0f, State.GlowTexture );
					CommandList.Blend( EDrawBlend::Add, 200 );
				}
			}
			CommandList.Blend( EDrawBlend::Opaque );
		}
		CommandList.UseLighting( true );
	}

	// 灯台の灯（光の計算を切った玉）と、右クリックで決めた目的地の輪
	void RecordMarkers( FDrawContext& CommandList, const FIslandState& State )
	{
		CommandList.UseLighting( false );
		CommandList.Sphere3D( GetLampPosition( State ), 0.55f, 16, LampColor );
		if ( State.bHasTarget )
		{
			constexpr int32 Segments = 24;
			const float32 Radius = 0.9f + 0.15f * NMath::Sin( State.Time * 6.0f );
			for ( int32 Index = 0; Index < Segments; ++Index )
			{
				const float32 A0 = static_cast<float32>( Index ) * NMath::TWO_PI / Segments;
				const float32 A1 = static_cast<float32>( Index + 1 ) * NMath::TWO_PI / Segments;
				const FVector3 P0 = State.Target + FVector3( NMath::Cos( A0 ) * Radius, 0.15f, NMath::Sin( A0 ) * Radius );
				const FVector3 P1 = State.Target + FVector3( NMath::Cos( A1 ) * Radius, 0.15f, NMath::Sin( A1 ) * Radius );
				CommandList.Line3D( P0, P1, TargetColor );
			}
		}
		CommandList.UseLighting( true );
	}

	// 空の明かり（真上からの、青みがかった弱い方向ライト）。影の絵の影を受けないので、影の中の地面も、うっすら照らす（影を真っ黒にしない）
	FLight MakeSkyLight( const FIslandState& State )
	{
		const float32 Dusk = NIslandScenery::GetDuskAmount( State );
		FLight Sky = FLight::MakeDirectional( FVector3( 0.0f, -1.0f, 0.0f ), FColor( 0.34f + 0.06f * Dusk, 0.38f - 0.04f * Dusk, 0.50f - 0.02f * Dusk ) );
		Sky.Specular = FColor::Black;
		Sky.ShadowMapSlots = 0;
		return Sky;
	}

	// 灯台の回る光（スポットライト）。夕方ほど強い。影の絵の影は受けない
	FLight MakeLighthouseLight( const FIslandState& State )
	{
		const float32 Angle = State.Time * 0.9f;
		const FVector3 Direction = FVector3( NMath::Cos( Angle ), -0.18f, NMath::Sin( Angle ) ).Normalized();
		const float32 Strength = 0.35f + 0.9f * NIslandScenery::GetDuskAmount( State );
		FLight Light = FLight::MakeSpot( GetLampPosition( State ), Direction, 0.45f, 0.25f, 60.0f, FColor( 1.0f * Strength, 0.88f * Strength, 0.55f * Strength ) );
		Light.ShadowMapSlots = 0;
		return Light;
	}
}

FVector3 NIslandScenery::GetSunDirection( const FIslandState& State )
{
	// 東（+x）から昇って、南を通り、西（-x）へ沈む
	const float32 Elevation = GetSunElevation( State );
	const float32 Azimuth = NMath::PI * State.DayTime;
	const float32 Horizontal = NMath::Cos( Elevation );
	return FVector3( -Horizontal * NMath::Cos( Azimuth ), -NMath::Sin( Elevation ), Horizontal * NMath::Sin( Azimuth ) * 0.6f ).Normalized();
}

float32 NIslandScenery::GetDuskAmount( const FIslandState& State )
{
	return 1.0f - NMath::Clamp( ( GetSunElevation( State ) - 0.18f ) / 0.7f, 0.0f, 1.0f );
}

FColor32 NIslandScenery::GetSkyTopColor( const FIslandState& State )
{
	return LerpColor( FColor32( 70, 132, 210 ), FColor32( 58, 54, 112 ), GetDuskAmount( State ) );
}

FColor32 NIslandScenery::GetHorizonColor( const FIslandState& State )
{
	return LerpColor( FColor32( 176, 210, 236 ), FColor32( 236, 150, 102 ), GetDuskAmount( State ) );
}

FVector3 NIslandScenery::GetCrystalPosition( const FIslandState& State, int32 Index )
{
	const FVector3& Base = State.Crystals[Index].Position;
	return Base + FVector3( 0.0f, 1.6f + 0.25f * NMath::Sin( State.Time * 2.2f + static_cast<float32>( Index ) * 1.3f ), 0.0f );
}

bool NIslandScenery::MakeShadowMap( FIslandState& State )
{
	State.ShadowMap = CShadowMap::Create( ShadowMapSize, ShadowMapSize );
	return State.ShadowMap.IsValid();
}

void NIslandScenery::RecordStage( FDrawContext& CommandList, const FIslandState& State )
{
	const bool bShadows = State.bShadows && State.ShadowMap.IsValid();
	const FVector3 SunDirection = GetSunDirection( State );

	// ① 影の絵: 太陽から見た深度。太陽も探検家もカメラも動くので、毎フレーム描き直す
	if ( bShadows )
	{
		const FShadowMapView View = FShadowMapView::MakeForCameraView( SunDirection, State.Camera, State.GetScreenViewport(), ShadowDistance, ShadowBottom, ShadowTop, ShadowMargin );
		FShadowMapPassScope Pass( CommandList, State.ShadowMap, View );
		RecordCasters( CommandList, State );
	}

	// ② ライトと霧: 太陽（時刻で色が変わる）・空の明かりとやわらかい光（影の中・裏側も真っ黒にしない）・灯台の回る光。霧は地平線の色に溶ける
	CommandList.DefaultLight( false );
	const float32 Dusk = GetDuskAmount( State );
	const FColor SunColor( 1.0f, 0.95f - 0.25f * Dusk, 0.86f - 0.42f * Dusk );
	FLightScope Sun( CommandList, FLight::MakeDirectional( SunDirection, SunColor ) );
	FLightScope Sky( CommandList, MakeSkyLight( State ) );
	FSoftLightingScope Soft( CommandList );
	FLightScope Lighthouse( CommandList, MakeLighthouseLight( State ) );
	if ( State.bFog )
	{
		CommandList.Fog( FFogSettings::MakeLinear( GetHorizonColor( State ), FogStart, FogEnd ) );
	}

	// ③ 影を受けるもの
	{
		FShadowMapUseScope Shadow( CommandList, 0, bShadows ? State.ShadowMap : TSharedPtr<CShadowMap>() );
		RecordCasters( CommandList, State );
	}

	// ④ 海（半透明）・自分で光るもの・目印。光のにじみの四角形が、後ろの海を隠さないよう、海を先に描く
	RecordSea( CommandList );
	RecordCrystals( CommandList, State );
	RecordMarkers( CommandList, State );
}
