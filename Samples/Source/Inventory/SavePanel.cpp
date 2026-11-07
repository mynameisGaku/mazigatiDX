#include "SavePanel.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Platform/System/ProjectPaths.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/InventoryUi.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	constexpr int32 NumSaveSlots = 3;
	constexpr int32 RowHeight = 40;
	const char* const SlotNames[NumSaveSlots] = { "InventorySlot1", "InventorySlot2", "InventorySlot3" };
	const char* const ButtonLabels[3] = { "Save", "Load", "Delete" };

	FUiRect GetButtonRect( int32 OriginX, int32 OriginY, int32 SlotIndex, int32 Button )
	{
		return FUiRect { OriginX + 160 + Button * 70, OriginY + SlotIndex * RowHeight, 64, RowHeight - 6 };
	}

	TArray<uint8> ToBytes( const NlohmannJson& Json )
	{
		const std::string Text = Json.dump();
		TArray<uint8> Bytes;
		for ( const char Character : Text )
		{
			Bytes.Add( static_cast<uint8>( Character ) );
		}
		return Bytes;
	}

	bool FromBytes( const TArray<uint8>& Bytes, NlohmannJson& OutJson )
	{
		const std::string Text( reinterpret_cast<const char*>( Bytes.GetData() ), static_cast<size_t>( Bytes.Num() ) );
		OutJson = NlohmannJson::parse( Text, nullptr, false );
		return !OutJson.is_discarded();
	}
}

FString CSavePanel::GetSaveDirectory()
{
	return FProjectPaths().GetSaveGamesDirectory();
}

CSavePanel::CSavePanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin )
	: State( InState )
	, Origin( InOrigin )
	, Storage( GetSaveDirectory() )
{}

void CSavePanel::OnSpawn()
{
	Refresh();
}

void CSavePanel::Refresh()
{
	Infos = Storage.List();
}

void CSavePanel::Save( int32 SlotIndex )
{
	if ( Storage.Write( SlotNames[SlotIndex], ToBytes( State->Model.ToJson() ) ) )
	{
		State->Say( FString::Printf( "Saved to slot %d.", SlotIndex + 1 ) );
	}
	else
	{
		State->Say( "Saving failed." );
	}
	Refresh();
}

void CSavePanel::Load( int32 SlotIndex )
{
	TArray<uint8> Bytes;
	const ESaveLoadResult Result = Storage.Read( SlotNames[SlotIndex], Bytes );
	if ( Result == ESaveLoadResult::NotFound )
	{
		State->Say( "That slot is empty." );
		return;
	}
	if ( Result != ESaveLoadResult::Ok && Result != ESaveLoadResult::RecoveredFromBackup )
	{
		State->Say( "That save is damaged and cannot be read." );
		return;
	}
	NlohmannJson Json;
	FInventoryModel Loaded;
	if ( !FromBytes( Bytes, Json ) || !FInventoryModel::FromJson( Json, Loaded ) )
	{
		State->Say( "That save has an unknown format." );
		return;
	}
	State->Held.Clear();
	State->Model = Loaded;
	State->Say( Result == ESaveLoadResult::RecoveredFromBackup ? FString::Printf( "Loaded slot %d from its backup (the main file was damaged).", SlotIndex + 1 ) : FString::Printf( "Loaded slot %d.", SlotIndex + 1 ) );
}

void CSavePanel::Delete( int32 SlotIndex )
{
	Storage.Delete( SlotNames[SlotIndex] );
	State->Say( FString::Printf( "Deleted slot %d.", SlotIndex + 1 ) );
	Refresh();
}

void CSavePanel::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	HotButton = -1;
	for ( int32 Slot = 0; Slot < NumSaveSlots; ++Slot )
	{
		for ( int32 Button = 0; Button < 3; ++Button )
		{
			if ( GetButtonRect( X, Y, Slot, Button ).Contains( Mouse.X, Mouse.Y ) )
			{
				HotButton = Slot * 3 + Button;
			}
		}
	}
	if ( HotButton >= 0 && Input->IsMouseButtonPressed( EMouseButton::Left ) && State->Held.IsEmpty() )
	{
		const int32 Slot = HotButton / 3;
		switch ( HotButton % 3 )
		{
		case 0:
			Save( Slot );
			break;
		case 1:
			Load( Slot );
			break;
		default:
			Delete( Slot );
			break;
		}
	}
}

void CSavePanel::Draw( FDrawContext& CommandList )
{
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	NInventoryUi::DrawTitle( CommandList, X, Y - 28, "Save slots" );
	for ( int32 Slot = 0; Slot < NumSaveSlots; ++Slot )
	{
		// 状態: 空 / 保存済み（大きさ・バックアップの有無）/ 壊れている
		const FSaveSlotInfo* Info = nullptr;
		for ( const FSaveSlotInfo& Candidate : Infos )
		{
			if ( Candidate.Name == SlotNames[Slot] )
			{
				Info = &Candidate;
			}
		}
		CommandList.TextF( X, Y + Slot * RowHeight + 6, FColor32::White, "Slot %d", Slot + 1 );
		FString Status = "empty";
		FColor32 StatusColor( 130, 134, 150 );
		if ( Info != nullptr )
		{
			Status = Info->bValid ? FString::Printf( "%d bytes%s", static_cast<int32>( Info->PayloadBytes ), Info->bHasBackup ? " + backup" : "" ) : FString( "DAMAGED" );
			StatusColor = Info->bValid ? FColor32( 150, 220, 160 ) : FColor32( 240, 110, 100 );
		}
		CommandList.Text( X + 70, Y + Slot * RowHeight + 6, *Status, StatusColor );
		for ( int32 Button = 0; Button < 3; ++Button )
		{
			const bool bEnabled = Button == 0 || Info != nullptr;
			NInventoryUi::DrawButton( CommandList, GetButtonRect( X, Y, Slot, Button ), ButtonLabels[Button], HotButton == Slot * 3 + Button, bEnabled );
		}
	}
}

bool CSavePanel::RunSelfCheck( const CSaveGameStorage& Storage )
{
	// 検査用のスロットに、保存 → 読み込み → 比べる → 消す。形の悪いデータ（存在しない道具・多すぎる個数）も、安全に読めるか
	bool bOk = true;
	const auto Check = [&bOk]( bool bCondition, const char* Name )
	{
		if ( !bCondition )
		{
			LOG( LogSamples, Warning, "Inventory self-test: failed: %s", Name );
			bOk = false;
		}
	};
	const char* Slot = "InventorySelfTest";
	FInventoryModel Original = FInventoryModel::MakeStarter();
	FString Message;
	Check( Original.UseSlot( 0, Message ), "use a potion" );
	Check( Original.UseSlot( 1, Message ), "equip the wooden sword" );
	Check( Original.Hp == 70 && Original.Equipment[0].Id == 3 && Original.GetAttack() == 8, "the potion healed and the sword raised the attack" );
	Check( Storage.Write( Slot, ToBytes( Original.ToJson() ) ), "write the save" );

	TArray<uint8> Bytes;
	NlohmannJson Json;
	FInventoryModel Loaded;
	Check( Storage.Read( Slot, Bytes ) == ESaveLoadResult::Ok, "read the save" );
	Check( FromBytes( Bytes, Json ) && FInventoryModel::FromJson( Json, Loaded ), "parse the save" );
	Check( Loaded.IsSameAs( Original ), "the loaded data is the same as the saved data" );

	// 形の悪いデータ: 存在しない道具の番号 99、多すぎる個数 500、負のお金。安全な値へ直る
	const NlohmannJson Bad = NlohmannJson::parse( R"({"Gold":-5,"Hp":999,"Slots":[[99,3],[1,500],[8,-2],"x"],"Equipment":[4,4,4]})" );
	FInventoryModel Repaired;
	Check( FInventoryModel::FromJson( Bad, Repaired ), "read the broken data" );
	Check( Repaired.Gold == 0 && Repaired.Hp == FInventoryModel::MaxHp, "negative gold and too much HP are repaired" );
	Check( Repaired.Slots[0].IsEmpty() && Repaired.Slots[2].IsEmpty(), "an unknown item and a negative count become empty" );
	Check( Repaired.Slots[1].Id == 1 && Repaired.Slots[1].Count == 9, "too many items become a full stack" );
	Check( !Repaired.Equipment[0].IsEmpty() && Repaired.Equipment[1].IsEmpty() && Repaired.Equipment[2].IsEmpty(), "only an item that fits the slot is equipped" );
	// 読めない形（Slots が無い）は、false
	Check( !FInventoryModel::FromJson( NlohmannJson::parse( R"({"Gold":1})" ), Repaired ), "data without slots is refused" );

	// 買う・売る
	FInventoryModel Shopper = FInventoryModel::MakeStarter();
	const int32 GoldBefore = Shopper.Gold;
	Check( Shopper.Buy( 2 ) && Shopper.Gold == GoldBefore - 60, "buy a Hi-Potion" );
	Check( Shopper.Buy( 6 ) == ( GoldBefore - 60 >= 150 ) , "buying with too little gold is refused" );
	Shopper.Gold = 10;
	Check( !Shopper.Buy( 6 ) && Shopper.Gold == 10, "not enough gold changes nothing" );
	FItemStack Gems;
	Gems.Id = 8;
	Gems.Count = 2;
	const int32 Before = Shopper.Gold;
	Check( Shopper.Sell( Gems ) == 200 && Shopper.Gold == Before + 200, "sell two gems for half the price each" );
	// いっぱいの持ち物には、買えても入らない
	FInventoryModel Full;
	for ( int32 Index = 0; Index < FInventoryModel::NumSlots; ++Index )
	{
		Full.Slots[Index].Id = 3;
		Full.Slots[Index].Count = 1;
	}
	Full.Gold = 1000;
	Check( !Full.Buy( 3 ) && Full.Gold == 1000, "a full inventory refuses a purchase without taking the gold" );
	// 存在しないスロットは、見つからない
	TArray<uint8> Nothing;
	Check( Storage.Read( "InventoryNoSuchSlot", Nothing ) == ESaveLoadResult::NotFound, "a missing slot is not found" );
	Storage.Delete( Slot );
	LOG( LogSamples, Log, "Inventory self-test: %s", bOk ? "PASS" : "FAIL" );
	return bOk;
}
