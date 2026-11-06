// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/SpriteSheet.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Texture/TextureLoader.h"

//=============================================================================
// CSpriteSheetLoader
//   スプライトシート（画像 + コマ割りとクリップの定義。書式は SpriteSheet.cpp の先頭）を読み込み・作成・保存する人。
//   画像は CTextureLoader で一緒に読み込む。画像の読み込みが終わるのを待つもの（非同期）は覚えておき、終わったら記録を片付ける。
//   使うときは、CAssetSubsystem::LoadSpriteSheet などから呼ばれる。
//=============================================================================
class CSpriteSheetLoader
{
public:
	CSpriteSheetLoader( CAssetLoadContext& InContext, CTextureLoader& InTextures );

	// 定義ファイル（JSON）から。画像も一緒に読み込む。読めなければ nullptr
	TSharedPtr<CSpriteSheet> Load( const FString& DefinitionPath, bool bAsync );

	// 画像から作る（コマ割りは Grid。クリップは AddClip で足す）。共有されず、呼ぶたびに新しいものが作られる
	TSharedPtr<CSpriteSheet> Create( const TSharedPtr<CTexture>& Texture, const FSpriteSheetGrid& Grid, const FString& Name );

	// コマを自分で足す（AddFrame）ための、空のスプライトシート
	TSharedPtr<CSpriteSheet> Create( const TSharedPtr<CTexture>& Texture, const FString& Name );

	// 定義（FSpriteSheetDefinition）から作る。画像は Definition.TexturePath から読み込む。共有されず、呼ぶたびに新しいものが作られる
	TSharedPtr<CSpriteSheet> Create( const FSpriteSheetDefinition& Definition, const FString& Name, bool bAsync );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FSpriteSheetDefinition& Definition );

	// 毎フレーム呼ぶ。画像の読み込みが終わった（または手放された）スプライトシートの記録を片付ける
	void Update();

	// 覚えているスプライトシート（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	// 読み込みが終わっていないスプライトシートを、覚えておく
	void AddPending( const TSharedPtr<CSpriteSheet>& Sheet );

	CAssetLoadContext& Context;
	CTextureLoader& Textures;
	TAssetCache<CSpriteSheet> Cache;
	TArray<TWeakPtr<CSpriteSheet>> Pending;
};
