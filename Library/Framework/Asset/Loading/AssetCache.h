// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Loading/AssetInfo.h"

//=============================================================================
// TAssetCache
//   読み込み済みのアセットを、キーで覚えておく人。持つのは弱い参照だけなので、使う人が全員手放せばアセットは解放される。
//   同じファイルを読み込み直すときに、生きているものを共有するために使う（各ローダーが、種類ごとに 1 つ持つ）。
//=============================================================================
template<typename T>
class TAssetCache
{
public:
	// キーのアセットが生きていれば返す。無い・手放されていれば nullptr
	TSharedPtr<T> Find( const FString& Key ) const
	{
		if ( const TWeakPtr<T>* Found = Entries.Find( Key ) )
		{
			return Found->Pin();
		}
		return nullptr;
	}

	void Add( const FString& Key, const TSharedPtr<T>& Asset )
	{
		Entries.Add( Key, Asset );
	}

	void Remove( const FString& Key )
	{
		Entries.Remove( Key );
	}

	void Clear()
	{
		Entries.Empty();
	}

	// 手放されたアセットの記録を片付ける（解放自体は、最後の参照が消えた時点で済んでいる）
	void Prune()
	{
		for ( auto It = Entries.CreateIterator(); It; ++It )
		{
			if ( !It.Value().IsValid() )
			{
				It.RemoveCurrent();
			}
		}
	}

	// 生きているアセットの情報を Out に足す
	void CollectInfos( TArray<FAssetInfo>& Out ) const
	{
		for ( auto It = Entries.CreateConstIterator(); It; ++It )
		{
			const TSharedPtr<T> Asset = It.Value().Pin();
			if ( !Asset.IsValid() )
			{
				continue;
			}

			FAssetInfo Info;
			Info.Type = Asset->GetAssetType();
			Info.Path = Asset->GetPath();
			Info.Description = Asset->Describe();
			Info.RefCount = Asset.GetSharedReferenceCount() - 1;   // 調べるために作った分を除く
			Info.bReady = Asset->IsReady();
			Info.MemoryBytes = Asset->GetMemoryBytes();
			Info.FileBytes = Asset->GetFileBytes();
			Info.LoadMs = Asset->GetLoadMs();
			Out.Add( Info );
		}
	}

private:
	TMap<FString, TWeakPtr<T>> Entries;
};
