// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/StateMachineAsset.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"

//=============================================================================
// CStateMachineLoader
//   ステートマシンの定義（.gxstate。書式は StateMachineDefinition.h の先頭）を読み込み・保存する人。読み込んだものは、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadStateMachine / SaveStateMachine から呼ばれる（直接使うことは少ない）。
//
//   - 読み込むときは、形の問題（捨てた項目など）をログに出す。ステートが 1 つも無い定義は、読み込みを失敗にする
//   - 部品の名前・ブラックボードのキーの検証は、実行用のインスタンスを作るとき（CStateMachine::Create）に行う
//=============================================================================
class CStateMachineLoader
{
public:
	explicit CStateMachineLoader( CAssetLoadContext& InContext );

	// 定義ファイル（JSON）から。読めない・ステートが無いときは nullptr
	TSharedPtr<CStateMachineAsset> Load( const FString& Path );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FStateMachineDefinition& Definition );

	// 覚えているステートマシン（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CStateMachineAsset> Cache;
};
