// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Sound/Sound.h"

//=============================================================================
// CSoundLoader
//   音のファイル（WAV・OGG・MP3 など）を読み込む人。効果音（Memory）か BGM（Stream）かの読み込み方ごとに、別の音として共有する。
//   PCM（生の音）の作成・読み出し・WAV の保存は CPcmSoundLoader。使うときは、CAssetSubsystem::LoadSound から呼ばれる。
//=============================================================================
class CSoundLoader
{
public:
	explicit CSoundLoader( CAssetLoadContext& InContext );

	// 音のファイルから。読み込み方が違えば別の音（同じファイルを、効果音としても BGM としても使えるように）。読めなければ nullptr
	TSharedPtr<CSound> Load( const FString& Path, ESoundLoadMode Mode, bool bAsync );

	// 覚えている音（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CSound> Cache;
};
