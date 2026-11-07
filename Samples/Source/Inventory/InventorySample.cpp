#include "InventorySample.h"

#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Platform/System/CommandLine.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Inventory/EquipmentPanel.h"
#include "Source/Inventory/InventoryGrid.h"
#include "Source/Inventory/InventoryOverlay.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/SavePanel.h"
#include "Source/Inventory/ShopPanel.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CInventorySample> Registrar( FSampleInfo { "Inventory", "Inventory: game UI and save data", "An inventory, equipment, a shop and save slots with the mouse. The rules (FInventoryModel) are separate from the panels; saving uses CSaveGameStorage (crash-safe, with a backup).", 97 } );

	// 描く順: パネル → 手前（持ち上げた山・ツールチップ・メッセージ）
	constexpr int32 PanelOrder = 10;
	constexpr int32 OverlayOrder = 100;
}

void CInventorySample::OnEnter()
{
	State = MakeShared<FInventoryState>();
	State->Say( "Welcome. Pick up items with the left button; use or equip with the right button." );

	SpawnObject<CInventoryGrid>( State, FVector2( 40.0f, 150.0f ) )->SetDrawOrder( PanelOrder );
	SpawnObject<CEquipmentPanel>( State, FVector2( 420.0f, 150.0f ) )->SetDrawOrder( PanelOrder );
	SpawnObject<CShopPanel>( State, FVector2( 900.0f, 150.0f ) )->SetDrawOrder( PanelOrder );
	SpawnObject<CSavePanel>( State, FVector2( 420.0f, 430.0f ) )->SetDrawOrder( PanelOrder );
	SpawnObject<CInventoryOverlay>( State )->SetDrawOrder( OverlayOrder );

	if ( NCommandLine::HasFlag( "selftest" ) )
	{
		CSavePanel::RunSelfCheck( CSaveGameStorage( CSavePanel::GetSaveDirectory() ) );
	}
}

void CInventorySample::OnExit()
{
	// 持ち上げたままで離れても、持ち物が消えないように、戻す（保存はしない）
	if ( State.IsValid() )
	{
		State->ReturnHeld();
	}
}

void CInventorySample::OnSampleDraw( FImmediateDraw& Screen )
{
}
