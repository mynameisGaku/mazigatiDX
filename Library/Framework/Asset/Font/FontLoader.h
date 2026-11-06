// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Framework/Asset/Font/FontStyle.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"

//=============================================================================
// CFontLoader
//   フォントを読み込む・作る人。フォントファイル（.ttf / .otf）から、入っているフォント名から、既定のフォントから作る。
//   大きさ・スタイルごとに別のフォントになり、同じ指定なら共有する。この環境にあるフォントを調べることもできる。
//   使うときは、CAssetSubsystem::LoadFont などから呼ばれる。
//=============================================================================
class CFontLoader
{
public:
	explicit CFontLoader( CAssetLoadContext& InContext );

	// フォントファイル（.ttf / .otf）から。ファイルは、このアプリの間だけ使えるフォントとして登録される
	TSharedPtr<CFont> LoadFile( const FString& FontFilePath, int32 Size, const FFontStyle& Style );

	// 入っているフォント名（"Meiryo" / "MS Gothic" など）から。名前が空なら既定のフォント
	TSharedPtr<CFont> LoadSystem( const FString& FaceName, int32 Size, const FFontStyle& Style );

	// その名前のフォントが、この環境（OS）にあるか（名前は UTF-8）
	bool IsAvailable( const FString& FaceName ) const;

	// この環境にあるフォントの名前の一覧
	TArray<FString> GetSystemFontNames() const;

	// 覚えているフォント（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	TSharedPtr<CFont> Create( const FString& Key, const FFontDesc& Desc, const FString& FontFilePath );

	CAssetLoadContext& Context;
	TAssetCache<CFont> Cache;
};
