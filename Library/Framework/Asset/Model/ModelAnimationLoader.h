// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Model/ModelAnimationAsset.h"
#include "Library/Framework/Asset/Model/ModelLoader.h"

//=============================================================================
// CModelAnimationLoader
//   モデルのアニメーションの定義（独自のデータアセット。書式は ModelAnimationAsset.h）を読み込み・作成・保存する人。
//   定義が指すモデルと、クリップごとのアニメーションのファイルは、CModelLoader で一緒に読み込む。読み込んだものは、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadModelAnimations などから呼ばれる。
//=============================================================================
class CModelAnimationLoader
{
public:
	CModelAnimationLoader( CAssetLoadContext& InContext, CModelLoader& InModels );

	// 定義ファイル（JSON）から。モデルと、クリップごとのアニメーションのファイルも一緒に読み込む。読めなければ nullptr
	TSharedPtr<CModelAnimationAsset> Load( const FString& DefinitionPath, bool bAsync );

	// 定義（FModelAnimationDefinition）から作る。共有されず、呼ぶたびに新しいものが作られる（エディタのプレビュー用）
	TSharedPtr<CModelAnimationAsset> Create( const FModelAnimationDefinition& Definition, const FString& Name, bool bAsync );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FModelAnimationDefinition& Definition );

	// 覚えているアニメーション集（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	CModelLoader& Models;
	TAssetCache<CModelAnimationAsset> Cache;
};
