#pragma once

#include "Library/Core/Types/Utility.h"

// Array.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Array.h の末尾から include される。直接 include しない

template<typename T>
auto TArray<T>::operator=( TArray&& Other ) noexcept -> TArray&
{
	if ( this != &Other )
	{
		NMemoryOps::DestructItems( Data, ArrayNum );
		FMemory::Free( Data );
		Data     = Other.Data;
		ArrayNum = Other.ArrayNum;
		ArrayMax = Other.ArrayMax;
		Other.Data     = nullptr;
		Other.ArrayNum = 0;
		Other.ArrayMax = 0;
	}
	return *this;
}

template<typename T>
void TArray<T>::SetNum( int32 NewNum )
{
	COLLECTION_CHECK( NewNum >= 0, "TArray::SetNum negative" );
	if ( NewNum > ArrayNum )
	{
		GrowTo( NewNum );
		NMemoryOps::DefaultConstructItems( Data + ArrayNum, NewNum - ArrayNum );
	}
	else
	{
		NMemoryOps::DestructItems( Data + NewNum, ArrayNum - NewNum );
	}
	ArrayNum = NewNum;
}

template<typename T>
void TArray<T>::SetNum( int32 NewNum, const T& Value )
{
	COLLECTION_CHECK( NewNum >= 0, "TArray::SetNum negative" );
	if ( NewNum > ArrayNum )
	{
		const T Copy = Value;
		GrowTo( NewNum );
		for ( int32 i = ArrayNum; i < NewNum; ++i ) new ( Data + i ) T( Copy );
	}
	else
	{
		NMemoryOps::DestructItems( Data + NewNum, ArrayNum - NewNum );
	}
	ArrayNum = NewNum;
}

template<typename T>
template<typename... Args>
T& TArray<T>::EmplaceGetRef( Args&&... args )
{
	if ( ArrayNum < ArrayMax )
	{
		new ( Data + ArrayNum ) T( Forward<Args>( args )... );
	}
	else
	{
		// args が自分の要素を参照していても安全なよう、新領域に先に構築してから移す
		const int32 NewMax  = CalculateGrowth( ArrayNum + 1 );
		T*          NewData = FMemory::AllocateArray<T>( NewMax );
		new ( NewData + ArrayNum ) T( Forward<Args>( args )... );
		NMemoryOps::RelocateItems( NewData, Data, ArrayNum );
		FMemory::Free( Data );
		Data     = NewData;
		ArrayMax = NewMax;
	}
	return Data[ArrayNum++];
}

template<typename T>
void TArray<T>::Append( const TArray& Other )
{
	if ( &Other == this )
	{
		const TArray Copy( Other );
		Append( Copy.Data, Copy.ArrayNum );
	}
	else
	{
		Append( Other.Data, Other.ArrayNum );
	}
}

template<typename T>
template<typename Predicate>
int32 TArray<T>::RemoveAll( Predicate Pred )
{
	int32 WriteIndex = 0;
	for ( int32 ReadIndex = 0; ReadIndex < ArrayNum; ++ReadIndex )
	{
		if ( Pred( Data[ReadIndex] ) ) continue;
		if ( WriteIndex != ReadIndex ) Data[WriteIndex] = MoveTemp( Data[ReadIndex] );
		++WriteIndex;
	}
	const int32 Removed = ArrayNum - WriteIndex;
	NMemoryOps::DestructItems( Data + WriteIndex, Removed );
	ArrayNum = WriteIndex;
	return Removed;
}

template<typename T>
void TArray<T>::RemoveAt( int32 Index, int32 Count )
{
	COLLECTION_CHECK( Index >= 0 && Count >= 0 && Index + Count <= ArrayNum, "TArray::RemoveAt out of range" );
	if ( Count == 0 ) return;

	if constexpr ( NMemoryOps::bIsBitwiseRelocatable<T> )
	{
		FMemory::Memmove( Data + Index, Data + Index + Count, sizeof( T ) * static_cast<usize>( ArrayNum - Index - Count ) );
	}
	else
	{
		for ( int32 i = Index; i + Count < ArrayNum; ++i ) Data[i] = MoveTemp( Data[i + Count] );
		NMemoryOps::DestructItems( Data + ArrayNum - Count, Count );
	}
	ArrayNum -= Count;
}

template<typename T>
void TArray<T>::Reallocate( int32 NewMax )
{
	COLLECTION_CHECK( NewMax >= ArrayNum, "TArray::Reallocate smaller than Num" );
	if ( NewMax == 0 )
	{
		FMemory::Free( Data );
		Data     = nullptr;
		ArrayMax = 0;
		return;
	}

	if constexpr ( NMemoryOps::bIsBitwiseRelocatable<T> )
	{
		Data = static_cast<T*>( FMemory::Realloc( Data, sizeof( T ) * static_cast<usize>( NewMax ), alignof( T ) ) );
	}
	else
	{
		T* NewData = FMemory::AllocateArray<T>( NewMax );
		NMemoryOps::RelocateItems( NewData, Data, ArrayNum );
		FMemory::Free( Data );
		Data = NewData;
	}
	ArrayMax = NewMax;
}

template<typename T>
void TArray<T>::OpenGap( int32 Index, int32 Count )
{
	if ( ArrayNum + Count > ArrayMax ) Reallocate( CalculateGrowth( ArrayNum + Count ) );

	if constexpr ( NMemoryOps::bIsBitwiseRelocatable<T> )
	{
		FMemory::Memmove( Data + Index + Count, Data + Index, sizeof( T ) * static_cast<usize>( ArrayNum - Index ) );
	}
	else
	{
		// 後ろから 1 つずつ移動構築
		for ( int32 i = ArrayNum - 1; i >= Index; --i )
		{
			new ( Data + i + Count ) T( MoveTemp( Data[i] ) );
			Data[i].~T();
		}
	}
	ArrayNum += Count;
}