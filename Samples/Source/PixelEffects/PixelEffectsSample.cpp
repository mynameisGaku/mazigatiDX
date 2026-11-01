// SPDX-License-Identifier: MIT
#include "PixelEffectsSample.h"

#include "Library/Framework/PixelEffect/Effects/PixelAuraEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelDamageNumberEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelExplosionEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelFireEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelHealEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelLightningEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelShockwaveEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelSlashEffect.h"
#include "Library/Framework/PixelEffect/Effects/PixelSparkleEffect.h"
#include "Library/Framework/PixelEffect/PixelStockActor.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/PixelEffects/PixelEffectTile.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPixelEffectsSample> Registrar( FSampleInfo { "PixelEffects", "PixelEffects: pixel-art effects", "Plays pixel-art effects (explosion, shockwave, damage number, sparkle, fire, heal, slash, lightning, aura) drawn pixel by pixel on the CPU and shown as images. F flips them, R restarts them with a new seed.", 61 } );

	// 見本の並べ方（左上・列の数・間隔。ピクセル）
	const FVector2 GridOrigin( 40.0f, 96.0f );
	constexpr int32 Columns = 5;
	constexpr float32 CellWidth = 232.0f;
	constexpr float32 CellHeight = 248.0f;
}

void CPixelEffectsSample::OnEnter()
{
	const FPixelActor Actor = NPixelStockActor::Make();

	// 格子の次の升へ、見本を 1 枚置く。立ち絵と重ねて見せるもの（状態変化）は bWithActor
	int32 Index = 0;
	const auto Place = [ & ]( TUniquePtr<IPixelEffect> Effect, EPixelBackdrop Backdrop, bool bWithActor )
	{
		const FVector2 Position = GridOrigin + FVector2( CellWidth * static_cast<float32>( Index % Columns ), CellHeight * static_cast<float32>( Index / Columns ) );
		SpawnObject<CPixelEffectTile>( Position, MoveTemp( Effect ), Backdrop, bWithActor ? &Actor : nullptr );
		++Index;
	};
	Place( MakeUnique<CPixelExplosionEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelShockwaveEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelDamageNumberEffect>( 1234, true ), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelSparkleEffect>(), EPixelBackdrop::Sky, false );
	Place( MakeUnique<CPixelFireEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelHealEffect>(), EPixelBackdrop::Ground, true );
	Place( MakeUnique<CPixelSlashEffect>(), EPixelBackdrop::Dark, false );
	Place( MakeUnique<CPixelLightningEffect>(), EPixelBackdrop::Ground, false );
	Place( MakeUnique<CPixelAuraEffect>( Actor.Mask ), EPixelBackdrop::Ground, true );
}

void CPixelEffectsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "PixelEffects: each effect draws into an IPixelSurface (color + light), is composited on the CPU and shown with CDynamicTexture.  F: flip  R: new seed", FColor32::Gray );
}
