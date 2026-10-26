#pragma once

#include "Library/Core/Types/Utility.h"

// Map.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Map.h の末尾から include される。直接 include しない

template<typename K, typename V, typename Hasher>
template<typename KArg, typename VArg>
V& TMap<K, V, Hasher>::AddImpl( KArg&& Key, VArg&& Value )
{
	bool bAdded = false;
	PairType& Pair = Table.FindOrAdd( Key, [&Key, &Value]()
	{
		return PairType { K( Forward<KArg>( Key ) ), V( Forward<VArg>( Value ) ) };
	}, bAdded );

	// 既にあった場合は値だけ上書き（Value はまだ消費されていない）
	if ( !bAdded ) Pair.Value = Forward<VArg>( Value );
	return Pair.Value;
}