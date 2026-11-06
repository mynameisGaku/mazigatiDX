// SPDX-License-Identifier: MIT
#include "PixelEffectsSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/PixelEffect/Effects/PixelAuraEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelBeamEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelCoinEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelDamageNumberEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelDashEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelDustEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelExplosionEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelFireEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelFreezeEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelHealEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelLevelUpEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelLightningEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelMagicCircleEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelPoisonEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelShockwaveEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelSlashEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelSparkleEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelSplashEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelWarpEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelWindEffect.h"
#include "Library/Framework/PixelEffect/Playback/PixelStockActor.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/PixelEffects/PixelEffectTile.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPixelEffectsSample> Registrar( FSampleInfo { "PixelEffects", "PixelEffects: pixel-art effects", "Plays 20 pixel-art effects (combat, elements, status, magic, pickups, text) drawn pixel by pixel on the CPU and shown as images. Left / Right change the page, F flips them, R restarts them with a new seed.", 61 } );

	// 見本の並べ方（左上・列の数・間隔。ピクセル）と、1 ページの枚数
	const FVector2 GridOrigin( 40.0f, 96.0f );
	constexpr int32 Columns = 5;
	constexpr int32 TilesPerPage = 10;
	constexpr float32 CellWidth = 232.0f;
	constexpr float32 CellHeight = 248.0f;
}

void CPixelEffectsSample::OnEnter()
{
	const FPixelActor Actor = NPixelStockActor::Make();

	// 格子の次の升へ、見本を 1 枚置く。立ち絵と重ねて見せるもの（状態変化など）は bWithActor
	const auto Place = [ & ]( TUniquePtr<IPixelEffect> Effect, EPixelBackdrop Backdrop, bool bWithActor )
	{
		const int32 Slot = Tiles.Num() % TilesPerPage;
		const FVector2 Position = GridOrigin + FVector2( CellWidth * static_cast<float32>( Slot % Columns ), CellHeight * static_cast<float32>( Slot / Columns ) );
		Tiles.Add( SpawnObject<CPixelEffectTile>( Position, MoveTemp( Effect ), Backdrop, bWithActor ? &Actor : nullptr ) );
	};
	Place( MakeUnique<CPixelExplosionEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelShockwaveEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelSlashEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelBeamEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelDashEffect>( Actor.Mask ), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelFireEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelLightningEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelFreezeEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelSplashEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelWindEffect>(), EPixelBackdrop::Sky, false );
	Place( MakeUnique<CPixelDustEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelHealEffect>(), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelPoisonEffect>(), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelAuraEffect>( Actor.Mask ), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelSparkleEffect>(), EPixelBackdrop::Sky, false );
	Place( MakeUnique<CPixelMagicCircleEffect>(), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelWarpEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelCoinEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelDamageNumberEffect>( 1234, true ), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelLevelUpEffect>(), EPixelBackdrop::Ground, true );
	ShowPage( 0 );
}

void CPixelEffectsSample::OnSampleTick( float32 DeltaTime )
{
	( void )DeltaTime;
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const int32 PageCount = ( Tiles.Num() + TilesPerPage - 1 ) / TilesPerPage;
	if ( Input->IsKeyPressed( EKey::Right ) && PageCount > 0 )
	{
		ShowPage( ( Page + 1 ) % PageCount );
	}
	if ( Input->IsKeyPressed( EKey::Left ) && PageCount > 0 )
	{
		ShowPage( ( Page + PageCount - 1 ) % PageCount );
	}
}

void CPixelEffectsSample::ShowPage( int32 InPage )
{
	Page = InPage;
	for ( int32 Index = 0; Index < Tiles.Num(); ++Index )
	{
		const bool bOnPage = Index / TilesPerPage == Page;
		Tiles[Index]->SetVisible( bOnPage );
		Tiles[Index]->SetTickEnabled( bOnPage );
	}
}

void CPixelEffectsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	const int32 PageCount = ( Tiles.Num() + TilesPerPage - 1 ) / TilesPerPage;
	Screen.Text( 16, 56, *FString::Printf( "Page %d / %d  (Left / Right: page  F: flip  R: new seed)   Each effect draws into an IPixelSurface (color + light) and is shown with CDynamicTexture.", Page + 1, PageCount ), FColor32::Gray );
}
