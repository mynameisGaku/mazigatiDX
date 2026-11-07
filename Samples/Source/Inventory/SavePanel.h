// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/SaveGame/SaveGameStorage.h"
#include "Library/Math/Math.h"

struct FInventoryState;

//=============================================================================
// CSavePanel
//   セーブ・ロード・削除（3 つのスロット）。保存は、CSaveGameStorage（壊れにくい保存。一時ファイルへ書いてから置き換え、
//   1 つ前のセーブを残し、壊れたファイルは、バックアップから読む）。中身は、FInventoryModel の JSON。
//   スロットごとに、状態（空 / 保存済み + 大きさ / 壊れている）を出す。
//=============================================================================
class CSavePanel : public CGameObject
{
public:
	CSavePanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	// 起動の検査（--selftest）: 保存 → 読み込みで、同じ中身になるか。ログに結果を出す
	static bool RunSelfCheck( const CSaveGameStorage& Storage );

	static FString GetSaveDirectory();

private:
	void Save( int32 SlotIndex );
	void Load( int32 SlotIndex );
	void Delete( int32 SlotIndex );

	// スロットの一覧を読み直す（保存・削除のあと）
	void Refresh();

	TSharedPtr<FInventoryState> State;
	FVector2 Origin;
	CSaveGameStorage Storage;
	TArray<FSaveSlotInfo> Infos;

	// ボタンの上にいるか（スロットの番号 * 3 + ボタンの番号。-1 は、どれでもない）
	int32 HotButton = -1;
};
