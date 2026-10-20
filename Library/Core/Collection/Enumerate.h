// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_ENUMERATE_H
#define FRAMEWORK_CORE_COLLECTION_ENUMERATE_H

#include <iterator>
#include <utility>

#include "Library/Core/Types/TypeDefinition.h"

// 元の要素への参照と 0 始まりの番号を組にして走査する。要素はコピーしない。
template<typename ContainerType>
class TEnumerateRange
{
	// 元の入れ物が返す反復子。
	using SourceIterator = decltype( std::begin( std::declval<ContainerType&>() ) );
	// ループの構造化束縛に渡す番号と参照。
	struct FEntry
	{
		// 走査順に振った番号。
		int32 Index;
		// const を保った元の要素。
		decltype( *std::declval<SourceIterator&>() ) Value;
	};
	// 元の反復子と番号を一緒に進める。
	class FIterator
	{
	public:
		// 元の位置と番号を受け取る。
		FORCEINLINE FIterator( SourceIterator InCurrent, int32 InIndex ) : Current( InCurrent ), Index( InIndex ) {}
		// 番号と元の要素への参照を返す。
		FORCEINLINE FEntry operator*() const { return { Index, *Current }; }
		// 元の位置と番号を一つ進める。
		FORCEINLINE FIterator& operator++() { ++Current; ++Index; return *this; }
		// 同じ入れ物の走査位置を比較する。
		FORCEINLINE bool operator!=( const FIterator& Other ) const { return Current != Other.Current; }
		// 同じ入れ物の走査位置を比較する。
		FORCEINLINE bool operator==( const FIterator& Other ) const { return Current == Other.Current; }
	private:
		// 現在の元の要素。
		SourceIterator Current;
		// 現在の走査番号。
		int32 Index;
	};

public:
	// 入れ物の寿命はこの範囲より長く保つ。
	FORCEINLINE explicit TEnumerateRange( ContainerType& InContainer ) : Container( InContainer ) {}
	// 先頭要素に番号 0 を付ける。
	FORCEINLINE FIterator begin() const { return FIterator( std::begin( Container ), 0 ); }
	// 元の終端まで走査する。
	FORCEINLINE FIterator end() const { return FIterator( std::end( Container ), 0 ); }

private:
	// 番号を付けて参照する入れ物。
	ContainerType& Container;
};

// 番号付きのループを作る。一時的な入れ物は受け付けない。
template<typename ContainerType>
FORCEINLINE TEnumerateRange<ContainerType> Enumerate( ContainerType& Container ) { return TEnumerateRange<ContainerType>( Container ); }

#endif
