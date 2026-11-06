// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/BlackboardAsset.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"

//=============================================================================
// CBlackboardLoader
//   ブラックボードの定義（.gxblackboard。書式は BlackboardDefinition.h の先頭）を読み込み・保存する人。読み込んだものは、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadBlackboard / SaveBlackboard から呼ばれる（直接使うことは少ない）。
//=============================================================================
class CBlackboardLoader
{
public:
	explicit CBlackboardLoader( CAssetLoadContext& InContext );

	// 定義ファイル（JSON）から。捨てたキーなどの問題はログに出す。読めなければ nullptr
	TSharedPtr<CBlackboardAsset> Load( const FString& Path );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FBlackboardDefinition& Definition );

	// 覚えているブラックボード（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CBlackboardAsset> Cache;
};
