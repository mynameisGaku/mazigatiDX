#include "IslandSample.h"

#include "Library/Framework/Asset/AssetManagerSubsystem.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Island/IslandClock.h"
#include "Source/Island/IslandCrystals.h"
#include "Source/Island/IslandExplorer.h"
#include "Source/Island/IslandHud.h"
#include "Source/Island/IslandScenery.h"
#include "Source/Island/IslandTerrain.h"
#include "Source/Island/IslandView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CIslandSample> Registrar( FSampleInfo { "Island", "Island: a small 3D adventure", "M3 put together in one game-like screen. An explorer walks around an island to collect glowing crystals: terrain from a vertex / index buffer, trees and a lighthouse from 3D shapes, a sun that crosses the sky with a shadow map, a rotating spot light, fog, a translucent sea, additive glows, a minimap and labels placed with WorldToScreen. WASD to walk (Shift to run), right click to walk to a spot (picking with ScreenToRay), drag to turn the camera, wheel to zoom; keys 1-3 switch shadows, fog and time", 60 } );

	// 描画順（小さいほうが奥）: 3D の島 → 表示
	constexpr int32 StageOrder = 0;
	constexpr int32 HudOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;

	// 光のにじみの画像の大きさ（画素）
	constexpr int32 GlowSize = 64;

	// 光のにじみ: 中心が明るく、外へなめらかに消える、水色の円（加算で重ねる）
	FPixelImage MakeGlowImage()
	{
		FPixelImage Image( GlowSize, GlowSize );
		const float32 Center = static_cast<float32>( GlowSize - 1 ) * 0.5f;
		for ( int32 Y = 0; Y < GlowSize; ++Y )
		{
			for ( int32 X = 0; X < GlowSize; ++X )
			{
				const float32 DeltaX = ( static_cast<float32>( X ) - Center ) / Center;
				const float32 DeltaY = ( static_cast<float32>( Y ) - Center ) / Center;
				const float32 Falloff = NMath::Clamp( 1.0f - NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY ), 0.0f, 1.0f );
				const float32 Strength = Falloff * Falloff;
				Image.Set( X, Y, FColor32( static_cast<uint8>( 90.0f * Strength ), static_cast<uint8>( 200.0f * Strength ), static_cast<uint8>( 255.0f * Strength ), 255 ) );
			}
		}
		return Image;
	}
}

void CIslandSample::OnEnter()
{
	CAssetManagerSubsystem* Assets = GetAppSubsystem<CAssetManagerSubsystem>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FIslandState>();

	// 島（決まった種から作るので、毎回同じ）と、描くためのバッファ・影の絵・画像
	NIslandTerrain::Generate( *State );
	if ( !NIslandTerrain::MakeBuffers( *State ) )
	{
		return;
	}
	NIslandScenery::MakeShadowMap( *State );
	State->GlowTexture = Assets->CreateDynamicTexture( "Samples/Island/Glow", MakeGlowImage() );
	State->MapTexture = Assets->CreateDynamicTexture( "Samples/Island/Map", NIslandTerrain::MakeMapImage( *State ) );
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	// 時間 → 探検家 → 結晶 の順に進める（拾う判定は、動いたあとの位置で）
	SpawnObject<CIslandClock>( State );
	SpawnObject<CIslandExplorer>( State );
	SpawnObject<CIslandCrystals>( State );
	SpawnObject<CIslandView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CIslandHud>( State, Font )->SetDrawOrder( HudOrder );
}

void CIslandSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Island: collect the crystals. WASD = walk, right click = walk there, drag = camera, wheel = zoom, 1-3 = settings.", FColor32::Gray );
}
