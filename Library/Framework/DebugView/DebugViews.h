// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Input/EKey.h"

//=============================================================================
// デバッグビュー（FDebugViewRegistry）
//   「画面に重ねて描く、デバッグ用の表示」（物理の形の線・AI の状態・経路など）の、オン / オフと調整項目の登録口。
//   表示の中身は、持ち主（物理なら Library/Physics）が描く。この登録口は、名前・オン/オフ・調整項目だけを持つので、
//   デバッグ画面（F1 のパネル）とコンソール（view コマンド）は、持ち主を知らずに、どのビューも同じように扱える（依存は、持ち主 → Framework の一方向）。
//
//   持ち主の側（例: 物理）:
//     static bool bEnabled = false;  static float32 Radius = 50.0f;
//     FDebugView View;
//     View.Name = "Physics";  View.Description = "Rigid body shapes";  View.pEnabled = &bEnabled;
//     View.bHasHotkey = true;  View.Hotkey = EKey::F4;
//     View.Options.Add( { "radius", FDebugViewOption::EType::Float, &Radius, 1.0f, 1000.0f, "Only draw within this distance" } );
//     FDebugViewRegistry::Global().Register( View );
//     // 描く側は、bEnabled を見て描く（CScene::AddSceneDraw など）
//
//   使う側: デバッグ画面のパネルと、コンソールの view / view.set（view Physics on、view.set Physics radius 30）。
//   - pEnabled と、項目の Value は、プロセスの終わりまで有効な場所（static など）を指す。メインスレッドだけから使う
//   - 名前は、大文字小文字を区別しない。同じ名前は置き換わる
//=============================================================================
struct FDebugViewOption
{
	enum class EType : uint8
	{
		Bool,
		Int,    // int32
		Float,  // float32
	};

	const char* Name = "";
	EType Type = EType::Bool;
	void* Value = nullptr;            // 実体へのポインター（Type に合う型: bool / int32 / float32）
	float32 Min = 0.0f;               // Int・Float の範囲（設定するとき、この範囲に収める）
	float32 Max = 1.0f;
	const char* Description = "";
};

struct FDebugView
{
	FString Name;
	FString Description;
	bool* pEnabled = nullptr;
	bool bHasHotkey = false;
	EKey Hotkey = EKey::F4;           // bHasHotkey が true のとき、このキーで、オン / オフを切り替える（デバッグ画面が見張る）
	TArray<FDebugViewOption> Options;
};

class FDebugViewRegistry
{
public:
	// プロセスで 1 つ
	static FDebugViewRegistry& Global();

	// 登録する（同じ名前は置き換える）。名前が空・pEnabled が無い・項目の名前が空か Value が無い、なら false
	bool Register( const FDebugView& View );
	bool Unregister( const FString& Name );

	int32 Num() const { return Views.Num(); }
	const FDebugView& Get( int32 Index ) const { return Views[Index]; }
	const FDebugView* Find( const FString& Name ) const;

	bool IsEnabled( const FString& Name ) const;
	// 見つからなければ false
	bool SetEnabled( const FString& Name, bool bEnabled ) const;
	// 切り替えて、切り替えたあとの状態を OutNow に入れる（省略できる）。見つからなければ false
	bool Toggle( const FString& Name, bool* OutNow = nullptr ) const;

	// 項目を文字列で設定する（Bool: on / off / true / false / 1 / 0、Int・Float: 数。範囲に収める）。
	// 見つからない・読めない（数でない・NaN・Bool に on / off 以外）なら false で、理由は OutError（値は変えない）
	bool SetOption( const FString& ViewName, const FString& OptionName, const FString& Text, FString* OutError = nullptr ) const;

	// ホットキーの表示用の名前（F1〜F12 は "F4" のように。それ以外は "key"）
	static FString GetKeyLabel( EKey Key );

	// 項目の、いまの値の文字列
	static FString GetOptionText( const FDebugViewOption& Option );

	// 項目を、名前で探す（大文字小文字を区別しない）。無ければ nullptr
	static const FDebugViewOption* FindOption( const FDebugView& View, const FString& OptionName );

private:
	TArray<FDebugView> Views;
};
