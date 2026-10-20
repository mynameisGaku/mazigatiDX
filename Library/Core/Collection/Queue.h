#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <utility>

#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Memory/Memory.h"

//=============================================================================
// TQueue<T>
//   先入れ先出しキュー（リングバッファ）。スレッドセーフではない。
//
//   TQueue<FEvent> Events;
//   Events.Enqueue( Event );
//   FEvent Next;
//   while ( Events.Dequeue( Next ) ) Handle( Next );
//=============================================================================
template<typename T>
class TQueue
{
public:
	using ElementType = T;

	//-------------------------------------------------------------------------
	// イテレータ（先頭から末尾へ）
	//-------------------------------------------------------------------------
	template<bool bConst>
	class TIterator
	{
		using QueueType = std::conditional_t<bConst, const TQueue, TQueue>;
		using RefType   = std::conditional_t<bConst, const T&, T&>;

	public:
		FORCEINLINE TIterator( QueueType* InQueue, int32 InIndex ) : Queue( InQueue ), Index( InIndex ) {}
		FORCEINLINE RefType operator*() const { return ( *Queue )[Index]; }
		FORCEINLINE TIterator& operator++() { ++Index; return *this; }
		bool operator!=( const TIterator& Other ) const { return Index != Other.Index; }
		bool operator==( const TIterator& Other ) const { return Index == Other.Index; }

	private:
		QueueType* Queue;
		int32      Index;
	};

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	TQueue() = default;

	FORCEINLINE TQueue( const TQueue& Other )
	{
		Reserve( Other.Count );
		for ( int32 i = 0; i < Other.Count; ++i ) Enqueue( Other[i] );
	}

	TQueue( TQueue&& Other ) noexcept
		: Data( Other.Data ), Capacity( Other.Capacity ), Head( Other.Head ), Count( Other.Count )
	{
		Other.Data     = nullptr;
		Other.Capacity = 0;
		Other.Head     = 0;
		Other.Count    = 0;
	}

	FORCEINLINE ~TQueue()
	{
		Empty();
		FMemory::Free( Data );
	}

	FORCEINLINE TQueue& operator=( const TQueue& Other )
	{
		if ( this != &Other )
		{
			Empty();
			Reserve( Other.Count );
			for ( int32 i = 0; i < Other.Count; ++i ) Enqueue( Other[i] );
		}
		return *this;
	}

	TQueue& operator=( TQueue&& Other ) noexcept;

	//-------------------------------------------------------------------------
	// サイズ
	//-------------------------------------------------------------------------
	FORCEINLINE int32 Num() const noexcept { return Count; }
	FORCEINLINE bool IsEmpty() const noexcept { return Count == 0; }

	// 全要素を削除（メモリは保持）
	FORCEINLINE void Empty()
	{
		while ( Count > 0 ) Pop();
		Head = 0;
	}

	FORCEINLINE void Reserve( int32 NewCapacity )
	{
		if ( NewCapacity > Capacity ) Grow( NewCapacity );
	}

	//-------------------------------------------------------------------------
	// 追加・取り出し
	//-------------------------------------------------------------------------
	FORCEINLINE void Enqueue( const T& Item ) { Emplace( Item ); }
	FORCEINLINE void Enqueue( T&& Item ) { Emplace( std::move( Item ) ); }

	template<typename... Args>
	T& Emplace( Args&&... args );

	// 先頭を取り出す。空なら false
	FORCEINLINE bool Dequeue( T& OutItem )
	{
		if ( Count == 0 ) return false;
		OutItem = std::move( Data[Head] );
		Pop();
		return true;
	}

	// 先頭を捨てる。空なら false
	FORCEINLINE bool Pop()
	{
		if ( Count == 0 ) return false;
		NMemoryOps::DestructItem( Data + Head );
		Head = ( Head + 1 ) % Capacity;
		--Count;
		return true;
	}

	// 先頭を覗く（空なら nullptr）
	FORCEINLINE T* Peek() { return Count > 0 ? Data + Head : nullptr; }
	FORCEINLINE const T* Peek() const { return Count > 0 ? Data + Head : nullptr; }

	// 先頭から Index 番目
	FORCEINLINE T& operator[]( int32 Index )
	{
		COLLECTION_CHECK( Index >= 0 && Index < Count, "TQueue index out of range" );
		return Data[PhysicalIndex( Index )];
	}
	FORCEINLINE const T& operator[]( int32 Index ) const
	{
		COLLECTION_CHECK( Index >= 0 && Index < Count, "TQueue index out of range" );
		return Data[PhysicalIndex( Index )];
	}

	FORCEINLINE TIterator<false> begin() { return TIterator<false>( this, 0 ); }
	FORCEINLINE TIterator<false> end() { return TIterator<false>( this, Count ); }
	FORCEINLINE TIterator<true> begin() const { return TIterator<true>( this, 0 ); }
	FORCEINLINE TIterator<true> end() const { return TIterator<true>( this, Count ); }

private:
	FORCEINLINE int32 PhysicalIndex( int32 LogicalIndex ) const { return ( Head + LogicalIndex ) % Capacity; }

	// 先頭が 0 番になるよう並べ直して容量を増やす
	void Grow( int32 NewCapacity );

	T*    Data     = nullptr;
	int32 Capacity = 0;
	int32 Head     = 0;  // 先頭の物理位置
	int32 Count    = 0;
};

#include "Queue.inl"
