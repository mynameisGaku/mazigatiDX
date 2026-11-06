// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/BehaviorTreeAsset.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"

//=============================================================================
// CBehaviorTreeLoader
//   ビヘイビアツリーの定義（.gxbt。書式は BehaviorTreeDefinition.h の先頭）を読み込み・保存する人。読み込んだものは、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadBehaviorTree / SaveBehaviorTree から呼ばれる（直接使うことは少ない）。
//
//   - 読み込むときは、形の問題（捨てた項目など）をログに出す。ルートが無い定義は、読み込みを失敗にする
//   - 部品の名前・ブラックボードのキーの検証は、実行用のインスタンスを作るとき（CBehaviorTree::Create）に行う
//=============================================================================
class CBehaviorTreeLoader
{
public:
	explicit CBehaviorTreeLoader( CAssetLoadContext& InContext );

	// 定義ファイル（JSON）から。読めない・ルートが無いときは nullptr
	TSharedPtr<CBehaviorTreeAsset> Load( const FString& Path );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FBehaviorTreeDefinition& Definition );

	// 覚えているビヘイビアツリー（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CBehaviorTreeAsset> Cache;
};
