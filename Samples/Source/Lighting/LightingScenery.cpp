#include "LightingScenery.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/PixelImage.h"

namespace
{
	// 床の大きさ（半分）と、市松模様の 1 周期（ワールドの単位。画像の 1 周期 = 2 x 2 マス）
	constexpr float32 FloorHalfSize = 30.0f;
	constexpr float32 FloorTileSize = 2.0f;

	// 球の分割数（細かいほど、頂点ごとの光がなめらかになる）
	constexpr int32 SphereDivisions = 32;

	// 球の半径と、並べる間隔
	constexpr float32 BallRadius = 1.2f;
	constexpr float32 BallSpacing = 3.2f;

	// 全体の環境光の強さの段階（4 キー）
	const float32 AmbientLevels[FLightingDemoState::NumAmbientLevels] = { 0.0f, 0.12f, 0.3f, 0.55f };

	// 街灯の色（6 色）
	const FColor StreetColors[FLightingDemoState::MaxExtraLamps] = {
		FColor( 1.0f, 0.3f, 0.3f ), FColor( 1.0f, 0.8f, 0.2f ), FColor( 0.3f, 1.0f, 0.4f ), FColor( 0.3f, 0.9f, 1.0f ), FColor( 0.45f, 0.45f, 1.0f ), FColor( 1.0f, 0.4f, 0.9f ),
	};

	// 回る電球の位置
	FVector3 GetLampPosition( float32 Time )
	{
		const float32 Angle = Time * 0.8f;
		return FVector3( NMath::Cos( Angle ) * 7.0f, 3.3f + NMath::Sin( Time * 1.7f ) * 0.5f, NMath::Sin( Angle ) * 7.0f );
	}

	// スポットライトの位置（手前の左の高いところ）と、いま照らしている床の点
	const FVector3 SpotlightPosition( -14.0f, 8.0f, -10.0f );

	FVector3 GetSpotlightTarget( float32 Time )
	{
		return FVector3( NMath::Sin( Time * 0.6f ) * 9.0f, 0.0f, NMath::Sin( Time * 0.37f ) * 5.0f + 2.0f );
	}

	// 街灯の位置（中庭の周り）
	FVector3 GetStreetLampPosition( int32 Index )
	{
		const float32 Angle = ( static_cast<float32>( Index ) * 60.0f + 30.0f ) * NMath::PI / 180.0f;
		return FVector3( NMath::Cos( Angle ) * 15.0f, 2.4f, NMath::Sin( Angle ) * 15.0f );
	}

	// 光の色を、図形の色（0..255）にする
	FColor32 ToShapeColor( const FColor& Color )
	{
		return Color.ToColor32();
	}

	// 足すライトを、足す順に並べる（月 -> 電球 -> スポットライト -> 街灯）。同時に効くのは先頭から 6 つまで
	TArray<FLight> MakeLights( const FLightingDemoState& State )
	{
		TArray<FLight> Lights;
		if ( State.bMoon )
		{
			Lights.Add( NLightingScenery::MakeMoon() );
		}
		if ( State.bLamp )
		{
			Lights.Add( NLightingScenery::MakeLamp( State.Time ) );
		}
		if ( State.bSpotlight )
		{
			Lights.Add( NLightingScenery::MakeSpotlight( State.Time ) );
		}
		for ( int32 Index = 0; Index < State.ExtraLamps; ++Index )
		{
			Lights.Add( NLightingScenery::MakeStreetLamp( Index ) );
		}
		return Lights;
	}

	// 光源の見た目: 光の計算を切って、光の色の小さな球を描く。上限を超えて効かないライトは、暗い灰色。
	// 光の計算を切っても、材質が図形の色を使わない設定（bUseVertexDiffuse が false）だと、図形の色ではなく材質の色が出るので、既定の材質に戻しておく
	void RecordBulb( FDrawContext& CommandList, const FLightingDemoState& State, const FVector3& Position, const FColor& Color, bool bActive )
	{
		CommandList.UseLighting( false );
		CommandList.SurfaceMaterial( FSurfaceMaterial() );
		CommandList.Sphere3D( Position, 0.3f, 16, bActive ? ToShapeColor( Color ) : FColor32( 60, 60, 66 ) );
		CommandList.UseLighting( State.bLighting );
	}

	// 床: 市松模様の画像を、全体に繰り返し貼る。大きな面なので、画素ごとの光（5 キー）でないと、電球やスポットライトの光の輪が出ない
	void RecordFloor( FDrawContext& CommandList, const FLightingDemoState& State )
	{
		if ( !State.FloorTexture.IsValid() )
		{
			return;
		}
		FDrawQuad3DData Quad;
		Quad.Right[0] = 1.0f;
		Quad.Up[1] = 0.0f;
		Quad.Up[2] = 1.0f;
		Quad.Width = FloorHalfSize * 2.0f;
		Quad.Height = FloorHalfSize * 2.0f;
		Quad.U1 = Quad.Width / FloorTileSize;
		Quad.V1 = Quad.Height / FloorTileSize;
		Quad.Handle = State.FloorTexture->GetImageHandle().Value;
		Quad.Orient = static_cast<int32>( EDrawQuadOrient::Fixed );
		CommandList.SurfaceMaterial( FSurfaceMaterial::MakeMatte() );
		CommandList.TextureAddress( EDrawTextureAddress::Wrap );
		CommandList.Filter( EDrawFilter::Anisotropic );
		CommandList.Quad3D( Quad );
		CommandList.Filter( EDrawFilter::Nearest );
	}

	// 壁・柱・箱: つや消しの材質（環境光を受ける）。光に向いた面は明るく、背を向けた面は暗い
	void RecordBlocks( FDrawContext& CommandList )
	{
		CommandList.SurfaceMaterial( FSurfaceMaterial::MakeMatte() );
		CommandList.Cube3D( FVector3( -20.0f, 0.0f, 19.0f ), FVector3( 20.0f, 5.0f, 20.0f ), FColor32( 150, 140, 135 ) );
		CommandList.Cube3D( FVector3( -11.0f, 0.0f, -6.0f ), FVector3( -8.0f, 2.4f, -3.0f ), FColor32( 170, 170, 180 ) );
		CommandList.Capsule3D( FVector3( 12.0f, 1.2f, 6.0f ), FVector3( 12.0f, 4.5f, 6.0f ), 1.1f, 24, FColor32( 175, 160, 140 ) );
		CommandList.Capsule3D( FVector3( -13.0f, 1.2f, 8.0f ), FVector3( -13.0f, 4.5f, 8.0f ), 1.1f, 24, FColor32( 175, 160, 140 ) );
	}

	// 材質の違う 5 つの球: つや消し・つやのある白・自分で光る球・プラスチック・金色の金属（材質の色を使う）
	void RecordBalls( FDrawContext& CommandList )
	{
		const float32 Y = BallRadius;
		const float32 StartX = -2.0f * BallSpacing;

		FSurfaceMaterial Matte = FSurfaceMaterial::MakeMatte();
		CommandList.SurfaceMaterial( Matte );
		CommandList.Sphere3D( FVector3( StartX, Y, 0.0f ), BallRadius, SphereDivisions, FColor32( 225, 70, 60 ), FColor32( 0, 0, 0 ) );

		FSurfaceMaterial Shiny = FSurfaceMaterial::MakeMatte();
		Shiny.Shininess = 80.0f;
		CommandList.SurfaceMaterial( Shiny );
		CommandList.Sphere3D( FVector3( StartX + BallSpacing, Y, 0.0f ), BallRadius, SphereDivisions, FColor32( 200, 205, 215 ), FColor32( 255, 255, 255 ) );

		CommandList.SurfaceMaterial( FSurfaceMaterial::MakeGlowing( FColor( 0.85f, 0.55f, 0.12f ) ) );
		CommandList.Sphere3D( FVector3( StartX + BallSpacing * 2.0f, Y, 0.0f ), BallRadius, SphereDivisions, FColor32( 80, 60, 20 ), FColor32( 0, 0, 0 ) );

		FSurfaceMaterial Plastic = FSurfaceMaterial::MakeMatte();
		Plastic.Shininess = 20.0f;
		CommandList.SurfaceMaterial( Plastic );
		CommandList.Sphere3D( FVector3( StartX + BallSpacing * 3.0f, Y, 0.0f ), BallRadius, SphereDivisions, FColor32( 60, 170, 95 ), FColor32( 200, 200, 200 ) );

		FSurfaceMaterial Gold = FSurfaceMaterial::MakeMatte();
		Gold.bUseVertexDiffuse = false;
		Gold.bUseVertexSpecular = false;
		Gold.Diffuse = FColor( 0.85f, 0.65f, 0.15f );
		Gold.Specular = FColor( 1.0f, 0.85f, 0.45f );
		Gold.Shininess = 40.0f;
		CommandList.SurfaceMaterial( Gold );
		CommandList.Sphere3D( FVector3( StartX + BallSpacing * 4.0f, Y, 0.0f ), BallRadius, SphereDivisions, FColor32( 0, 0, 0 ), FColor32( 0, 0, 0 ) );
	}
}

bool NLightingScenery::MakeTextures( CAssetManager& Assets, FLightingDemoState& State )
{
	// 床: 4 x 4 画素の濃淡の市松模様（1 周期 = 2 x 2 マス）
	FPixelImage Checker( 8, 8 );
	for ( int32 Y = 0; Y < 8; ++Y )
	{
		for ( int32 X = 0; X < 8; ++X )
		{
			const bool bLight = ( ( X / 4 + Y / 4 ) % 2 ) == 0;
			Checker.Set( X, Y, bLight ? FColor32( 170, 170, 178 ) : FColor32( 100, 104, 120 ) );
		}
	}
	State.FloorTexture = Assets.CreateDynamicTexture( "Samples/Lighting/Floor", Checker );
	return State.FloorTexture.IsValid();
}

FLight NLightingScenery::MakeMoon()
{
	FLight Moon = FLight::MakeDirectional( FVector3( 0.5f, -0.8f, 0.4f ), FColor( 0.4f, 0.45f, 0.68f ) );
	Moon.Specular = FColor( 0.25f, 0.3f, 0.45f );
	return Moon;
}

FLight NLightingScenery::MakeLamp( float32 Time )
{
	return FLight::MakePoint( GetLampPosition( Time ), 20.0f, FColor( 1.25f, 0.75f, 0.33f ) );
}

FLight NLightingScenery::MakeSpotlight( float32 Time )
{
	FLight Spotlight = FLight::MakeSpot( SpotlightPosition, GetSpotlightTarget( Time ) - SpotlightPosition, 0.7f, 0.3f, 45.0f, FColor( 1.9f, 1.95f, 2.1f ) );
	Spotlight.QuadraticAttenuation = 0.004f;
	return Spotlight;
}

FLight NLightingScenery::MakeStreetLamp( int32 Index )
{
	const int32 Safe = NMath::Clamp( Index, 0, FLightingDemoState::MaxExtraLamps - 1 );
	return FLight::MakePoint( GetStreetLampPosition( Safe ), 12.0f, StreetColors[Safe] * 1.4f );
}

int32 NLightingScenery::CountRequestedLights( const FLightingDemoState& State )
{
	return ( State.bMoon ? 1 : 0 ) + ( State.bLamp ? 1 : 0 ) + ( State.bSpotlight ? 1 : 0 ) + State.ExtraLamps;
}

int32 NLightingScenery::CountActiveLights( const FLightingDemoState& State )
{
	return NMath::Min( CountRequestedLights( State ), FLight::MaxActiveLights );
}

TArray<NLightingScenery::FStageLabel> NLightingScenery::MakeLabels( const FLightingDemoState& State )
{
	TArray<FStageLabel> Labels;
	const char* const BallNames[5] = { "Matte", "Shiny (shininess 80)", "Glowing (emissive)", "Plastic", "Gold (material colors)" };
	for ( int32 Index = 0; Index < 5; ++Index )
	{
		// 隣どうしの名札が重ならないように、1 つおきに高くする
		Labels.Add( FStageLabel { BallNames[Index], FVector3( ( static_cast<float32>( Index ) - 2.0f ) * BallSpacing, BallRadius * 2.0f + 0.5f + static_cast<float32>( Index % 2 ) * 1.1f, 0.0f ) } );
	}
	if ( State.bLamp )
	{
		Labels.Add( FStageLabel { "Lamp (point)", GetLampPosition( State.Time ) + FVector3( 0.0f, 0.9f, 0.0f ) } );
	}
	if ( State.bSpotlight )
	{
		Labels.Add( FStageLabel { "Spotlight", SpotlightPosition + FVector3( 0.0f, 1.0f, 0.0f ) } );
	}
	return Labels;
}

void NLightingScenery::RecordStage( FDrawContext& CommandList, const FLightingDemoState& State )
{
	// ライティングの設定。DxLib に最初からある 1 つのライトは切って、自分のライトだけで照らす
	CommandList.DefaultLight( false );
	CommandList.UseLighting( State.bLighting );
	const float32 Ambient = AmbientLevels[NMath::Clamp( State.AmbientLevel, 0, FLightingDemoState::NumAmbientLevels - 1 )];
	CommandList.AmbientLight( FColor( Ambient, Ambient, Ambient * 1.1f ) );
	FLightingOptions Options;
	Options.bPixelLighting = State.bPixelLighting;
	Options.bUseSpecular = State.bSpecular;
	Options.bWrapLighting = State.bWrapLighting;
	CommandList.LightingOptions( Options );

	// ライトを足す。足したライトは、この Draw が終わると、全部外れる
	const TArray<FLight> Lights = MakeLights( State );
	for ( const FLight& Light : Lights )
	{
		CommandList.PushLight( Light );
	}

	RecordFloor( CommandList, State );
	RecordBlocks( CommandList );
	RecordBalls( CommandList );

	// 光源の見た目（効かないライトは暗い灰色）と、スポットライトの向き
	int32 Slot = 0;
	for ( const FLight& Light : Lights )
	{
		const bool bActive = Slot < FLight::MaxActiveLights;
		if ( Light.Type != EDrawLightType::Directional )
		{
			RecordBulb( CommandList, State, Light.Position, Light.Diffuse, bActive );
		}
		if ( Light.Type == EDrawLightType::Spot )
		{
			CommandList.Line3D( Light.Position, GetSpotlightTarget( State.Time ), bActive ? FColor32( 200, 210, 230 ) : FColor32( 70, 70, 76 ) );
		}
		++Slot;
	}
}
