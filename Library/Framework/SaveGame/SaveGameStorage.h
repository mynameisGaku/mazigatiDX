// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/SaveGame/SaveGameTypes.h"

//=============================================================================
// CSaveGameStorage
//   セーブデータ（スロットごとのバイト列）を、壊れにくく保存・読み込みするクラス。何を保存するか（中身の形）は決めない。
//   中身は呼ぶ側が作る（JSON でもバイナリでもよい）。ゲームの処理（インベントリ・進行状況）は、サンプルや各ゲームのコードで書く。
//   置き場所は作るときに渡す（既定は FProjectPaths::GetSaveGamesDirectory()。検査では一時フォルダを渡す）。
//
//   CSaveGameStorage Storage( FProjectPaths().GetSaveGamesDirectory() );
//   Storage.Write( "Slot1", Bytes );                     // 書く
//   TArray<uint8> Loaded;
//   const ESaveLoadResult Result = Storage.Read( "Slot1", Loaded );
//   for ( const FSaveSlotInfo& Info : Storage.List() ) { ... }
//
//   - 書き込みは、まず一時ファイル（.tmp）に全部書いてから、置き換える。途中で電源が切れても、前のセーブが残る
//   - 置き換えるとき、いまの（壊れていない）セーブを 1 つ前のセーブ（.bak）にする。本体が壊れていたら、読み込みが .bak から読む
//   - ファイルは「識別子 + バージョン + 大きさ + CRC-32 + 中身」の形。途中で切れた・書き換わったファイルは、壊れたものとして扱う
//   - スロットの名前は、英数字・'_'・'-' の 1〜64 文字（パスの区切りや '..' は使えない）
//   - ゲームスレッドから使う（同じスロットを複数のスレッドから同時に書かない）
//=============================================================================
class CSaveGameStorage
{
public:
	// Directory: セーブデータを置くフォルダ（無ければ、書くときに作る）
	explicit CSaveGameStorage( const FString& InDirectory );

	const FString& GetDirectory() const
	{
		return Directory;
	}

	// スロットの名前として使えるか
	static bool IsValidSlotName( const FString& Slot );

	// Slot に Data を保存する。名前が不正・書けなければ false（前のセーブは壊さない）
	bool Write( const FString& Slot, const TArray<uint8>& Data ) const;

	// Slot を読む。結果は ESaveLoadResult。Ok / RecoveredFromBackup のときだけ Out に中身が入る（それ以外は Out を変えない）
	ESaveLoadResult Read( const FString& Slot, TArray<uint8>& Out ) const;

	// スロットの一覧（名前の順）。壊れているものも含み、bValid で見分ける
	TArray<FSaveSlotInfo> List() const;

	// スロットと、そのバックアップを消す。消す物が無くても、名前が正しければ true
	bool Delete( const FString& Slot ) const;

	// スロットがあるか（壊れていても、ファイルがあれば true）
	bool Exists( const FString& Slot ) const;

private:
	FString GetMainPath( const FString& Slot ) const;
	FString GetBackupPath( const FString& Slot ) const;
	FString GetTemporaryPath( const FString& Slot ) const;

	FString Directory;
};
