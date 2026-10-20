#pragma once

// HashTable.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// HashTable.h の末尾から include される。直接 include しない

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
auto THashTable<ElementType, KeyType, KeyFuncs, Hasher>::operator=( THashTable&& Other ) noexcept -> THashTable&
{
	if ( this != &Other )
	{
		Empty();
		Hashes   = Other.Hashes;
		Elements = Other.Elements;
		Capacity = Other.Capacity;
		Count    = Other.Count;
		Other.Hashes   = nullptr;
		Other.Elements = nullptr;
		Other.Capacity = 0;
		Other.Count    = 0;
	}
	return *this;
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
void THashTable<ElementType, KeyType, KeyFuncs, Hasher>::Reset()
{
	for ( int32 i = 0; i < Capacity; ++i )
	{
		if ( Hashes[i] != 0 )
		{
			NMemoryOps::DestructItem( Elements + i );
			Hashes[i] = 0;
		}
	}
	Count = 0;
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
int32 THashTable<ElementType, KeyType, KeyFuncs, Hasher>::FindIndex( const KeyType& Key ) const
{
	if ( Count == 0 ) return INDEX_NONE;

	const uint32 Hash = HashKey( Key );
	const int32  Mask = Capacity - 1;
	for ( int32 i = static_cast<int32>( Hash ) & Mask; ; i = ( i + 1 ) & Mask )
	{
		if ( Hashes[i] == 0 ) return INDEX_NONE;
		if ( Hashes[i] == Hash && KeyFuncs::GetKey( Elements[i] ) == Key ) return i;
	}
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
template<typename Constructor>
auto THashTable<ElementType, KeyType, KeyFuncs, Hasher>::FindOrAdd( const KeyType& Key, Constructor&& MakeElement, bool& bOutAdded ) -> ElementType&
{
	const int32 Existing = FindIndex( Key );
	if ( Existing != INDEX_NONE )
	{
		bOutAdded = false;
		return Elements[Existing];
	}

	// Key が自分の要素を参照している可能性があるので、再ハッシュ前に要素を作る
	alignas( ElementType ) unsigned char Temp[sizeof( ElementType )];
	ElementType* NewElement = new ( Temp ) ElementType( MakeElement() );
	const uint32 Hash = HashKey( KeyFuncs::GetKey( *NewElement ) );

	if ( CapacityFor( Count + 1 ) > Capacity ) Rehash( CapacityFor( Count + 1 ) );

	const int32 Index = FindEmptySlot( Hash );
	NMemoryOps::RelocateItems( Elements + Index, NewElement, 1 );
	Hashes[Index] = Hash;
	++Count;
	bOutAdded = true;
	return Elements[Index];
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
void THashTable<ElementType, KeyType, KeyFuncs, Hasher>::RemoveAt( int32 Index )
{
	COLLECTION_CHECK( Index >= 0 && Index < Capacity && Hashes[Index] != 0, "THashTable::RemoveAt invalid index" );

	NMemoryOps::DestructItem( Elements + Index );
	Hashes[Index] = 0;
	--Count;

	const int32 Mask = Capacity - 1;
	int32 Hole = Index;
	for ( int32 j = ( Index + 1 ) & Mask; Hashes[j] != 0; j = ( j + 1 ) & Mask )
	{
		const int32 Ideal = static_cast<int32>( Hashes[j] ) & Mask;
		// Hole が Ideal から j の間（循環）にあれば、j の要素を Hole へ移せる
		if ( ( ( j - Ideal ) & Mask ) >= ( ( j - Hole ) & Mask ) )
		{
			NMemoryOps::RelocateItems( Elements + Hole, Elements + j, 1 );
			Hashes[Hole] = Hashes[j];
			Hashes[j]    = 0;
			Hole = j;
		}
	}
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
template<typename Predicate>
int32 THashTable<ElementType, KeyType, KeyFuncs, Hasher>::RemoveIf( Predicate Pred )
{
	// イテレータと同じく空きスロットの次から 1 周する（循環したクラスタの要素を二重に判定しないため）
	int32 Removed = 0;
	const int32 Start = FindScanStart();
	const int32 Mask  = Capacity - 1;
	for ( int32 Step = 0; Step < Capacity; )
	{
		const int32 i = ( Start + Step ) & Mask;
		// 削除すると後続が i に詰められるので、同じ位置を再判定する
		if ( Hashes[i] != 0 && Pred( Elements[i] ) )
		{
			RemoveAt( i );
			++Removed;
		}
		else
		{
			++Step;
		}
	}
	return Removed;
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
void THashTable<ElementType, KeyType, KeyFuncs, Hasher>::Rehash( int32 NewCapacity )
{
	uint32*      OldHashes   = Hashes;
	ElementType* OldElements = Elements;
	const int32  OldCapacity = Capacity;

	Hashes   = FMemory::AllocateArray<uint32>( NewCapacity );
	Elements = FMemory::AllocateArray<ElementType>( NewCapacity );
	Capacity = NewCapacity;
	FMemory::Memzero( Hashes, sizeof( uint32 ) * static_cast<usize>( NewCapacity ) );

	for ( int32 i = 0; i < OldCapacity; ++i )
	{
		if ( OldHashes[i] == 0 ) continue;
		const int32 Index = FindEmptySlot( OldHashes[i] );
		NMemoryOps::RelocateItems( Elements + Index, OldElements + i, 1 );
		Hashes[Index] = OldHashes[i];
	}

	FMemory::Free( OldHashes );
	FMemory::Free( OldElements );
}

template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
void THashTable<ElementType, KeyType, KeyFuncs, Hasher>::CopyFrom( const THashTable& Other )
{
	if ( Other.Count == 0 ) return;

	Capacity = Other.Capacity;
	Count    = Other.Count;
	Hashes   = FMemory::AllocateArray<uint32>( Capacity );
	Elements = FMemory::AllocateArray<ElementType>( Capacity );
	FMemory::Memcpy( Hashes, Other.Hashes, sizeof( uint32 ) * static_cast<usize>( Capacity ) );
	for ( int32 i = 0; i < Capacity; ++i )
	{
		if ( Hashes[i] != 0 ) new ( Elements + i ) ElementType( Other.Elements[i] );
	}
}