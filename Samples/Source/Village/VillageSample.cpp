#include "VillageSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Village/Coin.h"
#include "Source/Village/Darkness.h"
#include "Source/Village/Dialogue.h"
#include "Source/Village/Player.h"
#include "Source/Village/StatusPanel.h"
#include "Source/Village/TileMap.h"
#include "Source/Village/VillageArt.h"
#include "Source/Village/Villager.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CVillageSample> Registrar( FSampleInfo { "Village", "Village: a small top-down game scene", "Walk around a small village (WASD / arrows, Shift to run). It combines tile maps, a camera (Transform2D), sprites, spinning coins, a dialogue box with wrapped text, night lighting (a render target with custom blending), and a minimap.", 30 } );

	// 描く順（小さいほうが奥）: 地図 → 金貨 → 村人 → 主人公 → 夜の暗さ → 情報の表示 → 会話の窓
	constexpr int32 MapOrder = 0;
	constexpr int32 CoinOrder = 10;
	constexpr int32 VillagerOrder = 20;
	constexpr int32 PlayerOrder = 30;
	constexpr int32 DarknessOrder = 50;
	constexpr int32 PanelOrder = 100;
	constexpr int32 DialogueOrder = 110;

	// 文字の大きさ（情報の表示・会話の本文・会話の名前）と、光の画像の大きさ、体力の輪の大きさと太さ
	constexpr int32 PanelFontSize = 22;
	constexpr int32 BodyFontSize = 20;
	constexpr int32 NameFontSize = 18;
	constexpr int32 GlowSize = 256;
	constexpr int32 RingSize = 48;
	constexpr int32 RingThickness = 7;
}

void CVillageSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 絵は、画素を CPU 側で作って、画像にする（画像のファイルを置かなくてもよい）
	const TSharedPtr<CDynamicTexture> Tileset = Assets->CreateDynamicTexture( "Samples/Village/Tileset", NVillageArt::MakeTileset() );
	const TSharedPtr<CDynamicTexture> PlayerSheet = Assets->CreateDynamicTexture( "Samples/Village/Player", NVillageArt::MakeCharacter( FColor32( 52, 120, 210 ), FColor32( 90, 56, 36 ) ) );
	const TSharedPtr<CDynamicTexture> VillagerSheet = Assets->CreateDynamicTexture( "Samples/Village/Villager", NVillageArt::MakeCharacter( FColor32( 190, 76, 66 ), FColor32( 226, 226, 232 ) ) );
	const TSharedPtr<CDynamicTexture> Coin = Assets->CreateDynamicTexture( "Samples/Village/Coin", NVillageArt::MakeCoin() );
	const TSharedPtr<CDynamicTexture> Glow = Assets->CreateDynamicTexture( "Samples/Village/Glow", NVillageArt::MakeGlow( GlowSize ) );
	const TSharedPtr<CDynamicTexture> Ring = Assets->CreateDynamicTexture( "Samples/Village/Ring", NVillageArt::MakeRing( RingSize, RingThickness ) );
	if ( !Tileset.IsValid() || !PlayerSheet.IsValid() || !VillagerSheet.IsValid() || !Coin.IsValid() || !Glow.IsValid() || !Ring.IsValid() )
	{
		return;
	}

	// 夜の層（アルファを持つ描画先。画面と同じ大きさ）
	FRenderTargetDesc NightDesc;
	NightDesc.Width = FVillageState::ViewLeft + FVillageState::ViewWidth;
	NightDesc.Height = FVillageState::ViewTop + FVillageState::ViewHeight;
	NightDesc.bAlpha = true;
	NightDesc.bDepth = false;
	const TSharedPtr<CRenderTarget> NightLayer = Assets->CreateRenderTarget( "Samples/Village/NightLayer", NightDesc );

	const TSharedPtr<CFont> PanelFont = Assets->GetDefaultFont( PanelFontSize );
	const TSharedPtr<CFont> BodyFont = Assets->GetDefaultFont( BodyFontSize );
	const TSharedPtr<CFont> NameFont = Assets->GetDefaultFont( NameFontSize );

	// 状態を分け合う入れ物と、地図
	State = MakeShared<FVillageState>();
	const TSharedPtr<CTileMap> Map = SpawnObject<CTileMap>( State, Tileset );
	if ( !Map.IsValid() )
	{
		return;
	}
	Map->SetDrawOrder( MapOrder );
	const TSharedPtr<CDynamicTexture> Overview = Assets->CreateDynamicTexture( "Samples/Village/Overview", Map->MakeOverview() );

	// 金貨
	State->CoinPositions = Map->GetCoinPositions();
	State->CoinCollected.SetNum( State->CoinPositions.Num(), false );
	for ( int32 Index = 0; Index < State->CoinPositions.Num(); ++Index )
	{
		SpawnObject<CCoin>( State, Index, State->CoinPositions[Index], Coin, Glow )->SetDrawOrder( CoinOrder );
	}

	// 村人・主人公・夜の暗さ・情報の表示・会話の窓
	SpawnObject<CVillager>( State, Map->GetVillagerPosition(), VillagerSheet, PanelFont )->SetDrawOrder( VillagerOrder );
	SpawnObject<CPlayer>( State, Map, PlayerSheet )->SetDrawOrder( PlayerOrder );
	SpawnObject<CDarkness>( State, Map->GetLampPositions(), NightLayer, Glow )->SetDrawOrder( DarknessOrder );
	SpawnObject<CStatusPanel>( State, PanelFont, Coin, Ring, Overview )->SetDrawOrder( PanelOrder );
	SpawnObject<CDialogue>( State, BodyFont, NameFont )->SetDrawOrder( DialogueOrder );
}

void CVillageSample::OnSampleTick( float32 DeltaTime )
{
	if ( State.IsValid() )
	{
		State->Time += DeltaTime;
	}
}

void CVillageSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Village: WASD / arrows move, Shift runs, Enter talks, N switches day / night. It walks by itself until you press a key.", FColor32::Gray );
}
