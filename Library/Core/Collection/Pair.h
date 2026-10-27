#pragma once
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Hash.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// TPair<K, V>
//   キーと値の組。TMap の要素型。構造化束縛で取り出せる:
//   for ( auto& [Key, Value] : Map ) ...
//=============================================================================
template<typename K, typename V>
struct TPair
{
	K Key;
	V Value;

	friend bool operator==( const TPair& A, const TPair& B ) { return A.Key == B.Key && A.Value == B.Value; }
	friend bool operator!=( const TPair& A, const TPair& B ) { return !( A == B ); }
	friend FORCEINLINE bool operator<( const TPair& A, const TPair& B ) { return A.Key < B.Key || ( !( B.Key < A.Key ) && A.Value < B.Value ); }

	friend FORCEINLINE uint32 GetTypeHash( const TPair& Pair ) { return HashCombine( GetTypeHash( Pair.Key ), GetTypeHash( Pair.Value ) ); }
};

template<typename K, typename V>
FORCEINLINE TPair<TDecayT<K>, TDecayT<V>> MakePair( K&& Key, V&& Value )
{
	return { Forward<K>( Key ), Forward<V>( Value ) };
}
