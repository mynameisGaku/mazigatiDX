// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/EffectAsset.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"

//=============================================================================
// CEffectLoader
//   パーティクルのエフェクト（.gxeffect。書式は ParticleEffect.cpp の先頭）を読み込み・保存する人。読み込んだものは、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadEffect / SaveEffect から呼ばれる（直接使うことは少ない）。
//=============================================================================
class CEffectLoader
{
public:
	explicit CEffectLoader( CAssetLoadContext& InContext );

	// 定義ファイル（JSON）から。中の画像は、再生を始めるとき（ParticleEffectBackend）に読み込まれる。読めなければ nullptr
	TSharedPtr<CEffectAsset> Load( const FString& Path );

	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の Load は読み直す
	bool Save( const FString& Path, const FParticleEffectDefinition& Definition );

	// 覚えているエフェクト（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CEffectAsset> Cache;
};
