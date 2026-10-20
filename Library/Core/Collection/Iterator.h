// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_ITERATOR_H
#define FRAMEWORK_CORE_COLLECTION_ITERATOR_H

#include <memory>
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/CollectionCommon.h"

// Num と添字を持つ配列を走査する。削除後は次の ++ まで要素を参照しない。
template<typename ContainerType>
class TIterator
{
public:
	// 添字アクセスと同じ参照型を使い、const を保つ。
	using ReferenceType = decltype( std::declval<ContainerType&>()[0] );
	// 要素を所有せず、配列と開始位置を受け取る。範囲外の開始位置は不正。
	FORCEINLINE explicit TIterator( ContainerType& InContainer, int32 InIndex = 0 ) : Container( &InContainer ), Index( InIndex )
	{
		COLLECTION_CHECK( Index >= 0 && Index <= Container->Num(), "TIterator invalid start index" );
	}

	// 有効な現在要素を返す。終端または削除直後は不正。
	FORCEINLINE ReferenceType operator*() const
	{
		COLLECTION_CHECK( static_cast<bool>( *this ) && !bCurrentRemoved, "TIterator has no current element" );
		return ( *Container )[Index];
	}
	// 現在要素のアドレスを返す。終端または削除直後は不正。
	FORCEINLINE auto operator->() const { return std::addressof( operator*() ); }
	// 削除で詰められた要素も次に訪問する。
	FORCEINLINE TIterator& operator++()
	{
		if ( bCurrentRemoved ) bCurrentRemoved = false;
		else if ( Index < Container->Num() ) ++Index;
		return *this;
	}
	// 現在位置が配列内かを返す。
	FORCEINLINE explicit operator bool() const { return Index >= 0 && Index < Container->Num(); }
	// 現在の添字を返す。削除直後は詰められた位置を返す。
	FORCEINLINE int32 GetIndex() const { return Index; }
	// 現在要素を順序を保って削除する。const 配列では使用できない。
	FORCEINLINE void RemoveCurrent()
	{
		static_assert( !std::is_const_v<ContainerType>, "RemoveCurrent requires a mutable container" );
		COLLECTION_CHECK( static_cast<bool>( *this ) && !bCurrentRemoved, "TIterator has no current element" );
		Container->RemoveAt( Index );
		bCurrentRemoved = true;
	}
	// 現在要素を末尾と入れ替えて削除する。const 配列では使用できない。
	FORCEINLINE void RemoveCurrentSwap()
	{
		static_assert( !std::is_const_v<ContainerType>, "RemoveCurrentSwap requires a mutable container" );
		COLLECTION_CHECK( static_cast<bool>( *this ) && !bCurrentRemoved, "TIterator has no current element" );
		Container->RemoveAtSwap( Index );
		bCurrentRemoved = true;
	}

private:
	// 要素と配列の寿命は呼び出し側が維持する。
	ContainerType* Container;
	// 現在の配列位置。
	int32 Index;
	// 削除した位置を次の ++ で再確認する。
	bool bCurrentRemoved = false;
};

#endif
