#pragma once

#include "Library/Core/Types/Utility.h"

// Queue.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Queue.h の末尾から include される。直接 include しない

template<typename T>
auto TQueue<T>::operator=( TQueue&& Other ) noexcept -> TQueue&
{
	if ( this != &Other )
	{
		Empty();
		FMemory::Free( Data );
		Data     = Other.Data;
		Capacity = Other.Capacity;
		Head     = Other.Head;
		Count    = Other.Count;
		Other.Data     = nullptr;
		Other.Capacity = 0;
		Other.Head     = 0;
		Other.Count    = 0;
	}
	return *this;
}

template<typename T>
template<typename... Args>
T& TQueue<T>::Emplace( Args&&... args )
{
	if ( Count == Capacity )
	{
		// args が自分の要素を参照していても安全なよう、先に値を作る
		T NewItem( Forward<Args>( args )... );
		Grow( Capacity < 4 ? 8 : Capacity * 2 );
		T* Slot = Data + PhysicalIndex( Count );
		new ( Slot ) T( MoveTemp( NewItem ) );
		++Count;
		return *Slot;
	}

	T* Slot = Data + PhysicalIndex( Count );
	new ( Slot ) T( Forward<Args>( args )... );
	++Count;
	return *Slot;
}

template<typename T>
void TQueue<T>::Grow( int32 NewCapacity )
{
	T* NewData = FMemory::AllocateArray<T>( NewCapacity );
	for ( int32 i = 0; i < Count; ++i )
	{
		NMemoryOps::RelocateItems( NewData + i, Data + PhysicalIndex( i ), 1 );
	}
	FMemory::Free( Data );
	Data     = NewData;
	Capacity = NewCapacity;
	Head     = 0;
}