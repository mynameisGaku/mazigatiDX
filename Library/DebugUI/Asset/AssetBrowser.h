// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/AssetReferences.h"

//=============================================================================
// アセットのブラウザー（Project ウィンドウ）
//   アセットのルートフォルダーの下のファイルを、種類別に一覧する。ダブルクリック（か、右クリック > Open in editor）で、
//   その種類のエディタで開く（開く処理は、呼び出し側が渡す）。エディタ専用の実行ファイル（Editor.exe）の入口。
//
//   - 種類は拡張子で決まる: .gxsprite（スプライトシート）・.gxmodelanim（モデルのアニメーション）・.gxworld（ワールド）・.gxeffect（エフェクト）・
//     .gxblackboard / .gxstate / .gxbt（AI）。画像・音・モデルは、一覧に出すが、エディタは無い
//   - 絞り込み: 名前の一部と、種類の分類
//   - 一覧は、3 秒ごと（と、Rescan を押したとき）に作り直す
//=============================================================================
enum class EAssetBrowserKind : uint8
{
	SpriteSheet,
	ModelAnimation,
	World,
	Effect,
	Blackboard,
	StateMachine,
	BehaviorTree,
	PartsCatalog,
	Texture,
	Model,
	Sound,
};

struct FAssetBrowserEntry
{
	FString Path;
	EAssetBrowserKind Kind = EAssetBrowserKind::Texture;
	int64 Size = 0;
};

struct FAssetBrowserState
{
	FString Filter;
	// 分類（0 全部 / 1 エディタがあるもの / 2 スプライトシート / 3 アニメーション / 4 ワールド / 5 エフェクト / 6 AI / 7 画像 / 8 モデル / 9 音）
	int32 Category = 1;
	TArray<FAssetBrowserEntry> Entries;
	double LastScanTime = -100.0;
	FString ScannedRoot;
	FString SelectedPath;
	FString StatusMessage;
	bool bStatusIsError = false;

	// 改名・削除の確認ウィンドウ（右クリックの Rename / Delete。ファイルを指しているアセットの一覧つき）
	enum class EDialog : uint8 { None, Rename, Delete } Dialog = EDialog::None;
	bool bOpenDialogRequested = false;
	FString DialogPath;                   // 対象のファイル
	FString RenameNewPath;
	TArray<FAssetReference> DialogReferences;
};

namespace NDebugUI
{
	// 拡張子（パスの末尾）から、種類を決める。知らない拡張子なら false
	bool TryGetAssetBrowserKind( const FString& Path, EAssetBrowserKind& OutKind );
	// その種類に、エディタがあるか
	bool HasEditor( EAssetBrowserKind Kind );
	const char* ToString( EAssetBrowserKind Kind );

	// 「このファイルを、その種類のエディタで開いて」という要求（ツール同士の連携）。どのエディタからも呼べる（例: ワールドエディタのエフェクトの欄の Open ボタン）。
	// 要求は 1 つだけ覚え（あとの要求が、前の要求を置き換える）、DrawAssetTools が、次のフレームの頭で OpenAssetInEditor に渡す。ゲームスレッドから呼ぶ
	void RequestOpenAssetInEditor( const FString& AssetPath );
	// 覚えている要求を取り出す（取り出すと空になる）。無ければ false
	bool TakeOpenAssetRequest( FString& OutPath );

	// ウィンドウを 1 つ出す。OnOpen( パス ) は、エディタで開く処理（開けなければ false。理由は Status に出してよい）。
	// IsOpenInEditor( パス ) は、そのファイルが、どれかのエディタで開かれているか（開かれているファイルは、改名・削除させない）
	void DrawAssetBrowser( FAssetBrowserState& State, const TCallable<bool( const FString& )>& OnOpen, const TCallable<bool( const FString& )>& IsOpenInEditor, bool* pOpen = nullptr );
}
