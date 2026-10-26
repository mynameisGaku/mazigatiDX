// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_ARRAY_H
#define FRAMEWORK_CORE_COLLECTION_ARRAY_H
#include "Library/Core/Types/CompilerMacros.h"
#include <initializer_list>
#include <utility>

#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Collection/Iterator.h"
#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// TArray<T>
//   可変長配列。要素は連続したメモリに並ぶ。インデックス・個数は int32。
//
//   TArray<int32> Numbers = { 3, 1, 2 };
//   Numbers.Add( 5 );
//   Numbers.Sort();
//   if ( Numbers.Contains( 3 ) ) ...
//   Numbers.RemoveAll( []( int32 n ){ return n % 2 == 0; } );
//   for ( int32 n : Numbers ) ...
//
//   ※ 要素の追加で再確保が起きると、要素へのポインタ・参照は無効になる
//=============================================================================
template<typename T>
class TArray
{
public:
	using ElementType    = T;
	using iterator       = T*;
	using const_iterator = const T*;
	// 要素を削除しながら進める配列の反復子。
	using Iterator = TIterator<TArray>;
	// 要素を書き換えない配列の反復子。
	using ConstIterator = TIterator<const TArray>;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	TArray() = default;

	FORCEINLINE TArray( std::initializer_list<T> List ) { Append( List.begin(), static_cast<int32>( List.size() ) ); }
	FORCEINLINE TArray( const T* Ptr, int32 Count ) { Append( Ptr, Count ); }

	template<typename InputIt, typename = TEnableIfT<!bIsIntegral<InputIt>>>
	FORCEINLINE TArray( InputIt First, InputIt Last )
	{
		for ( ; First != Last; ++First ) Emplace( *First );
	}

	FORCEINLINE TArray( const TArray& Other ) { Append( Other.Data, Other.ArrayNum ); }

	TArray( TArray&& Other ) noexcept
		: Data( Other.Data ), ArrayNum( Other.ArrayNum ), ArrayMax( Other.ArrayMax )
	{
		Other.Data     = nullptr;
		Other.ArrayNum = 0;
		Other.ArrayMax = 0;
	}

	FORCEINLINE ~TArray()
	{
		NMemoryOps::DestructItems( Data, ArrayNum );
		FMemory::Free( Data );
	}

	FORCEINLINE TArray& operator=( const TArray& Other )
	{
		if ( this != &Other )
		{
			Reset();
			Append( Other.Data, Other.ArrayNum );
		}
		return *this;
	}

	TArray& operator=( TArray&& Other ) noexcept;

	FORCEINLINE TArray& operator=( std::initializer_list<T> List )
	{
		Reset();
		Append( List.begin(), static_cast<int32>( List.size() ) );
		return *this;
	}

	// Count 個の Value で初期化（既存要素は破棄）
	FORCEINLINE void Init( const T& Value, int32 Count )
	{
		const T Copy = Value;  // Value が自分の要素を指していても安全なように
		Reset();
		Reserve( Count );
		for ( int32 i = 0; i < Count; ++i ) new ( Data + i ) T( Copy );
		ArrayNum = Count;
	}

	//-------------------------------------------------------------------------
	// サイズ
	//-------------------------------------------------------------------------
	FORCEINLINE int32 Num() const noexcept { return ArrayNum; }
	FORCEINLINE int32 Max() const noexcept { return ArrayMax; }
	FORCEINLINE bool IsEmpty() const noexcept { return ArrayNum == 0; }
	FORCEINLINE bool IsValidIndex( int32 Index ) const noexcept { return Index >= 0 && Index < ArrayNum; }

	// 要素数を変更（増えた分は値初期化）
	void SetNum( int32 NewNum );

	void SetNum( int32 NewNum, const T& Value );

	// 容量を Capacity 以上にする
	FORCEINLINE void Reserve( int32 Capacity )
	{
		if ( Capacity > ArrayMax ) Reallocate( Capacity );
	}

	// 余分な容量を解放
	FORCEINLINE void Shrink()
	{
		if ( ArrayMax > ArrayNum ) Reallocate( ArrayNum );
	}

	// 全要素を削除してメモリも解放（Slack 分は確保し直す）
	FORCEINLINE void Empty( int32 Slack = 0 )
	{
		NMemoryOps::DestructItems( Data, ArrayNum );
		ArrayNum = 0;
		if ( ArrayMax != Slack ) Reallocate( Slack );
	}

	// 全要素を削除（メモリは保持）
	FORCEINLINE void Reset() noexcept
	{
		NMemoryOps::DestructItems( Data, ArrayNum );
		ArrayNum = 0;
	}

	//-------------------------------------------------------------------------
	// アクセス
	//-------------------------------------------------------------------------
	FORCEINLINE T& operator[]( int32 Index )
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "TArray index out of range" );
		return Data[Index];
	}
	FORCEINLINE const T& operator[]( int32 Index ) const
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "TArray index out of range" );
		return Data[Index];
	}

	// 末尾から IndexFromEnd 番目
	FORCEINLINE T& Last( int32 IndexFromEnd = 0 ) { return ( *this )[ArrayNum - 1 - IndexFromEnd]; }
	FORCEINLINE const T& Last( int32 IndexFromEnd = 0 ) const { return ( *this )[ArrayNum - 1 - IndexFromEnd]; }
	FORCEINLINE T& First() { return ( *this )[0]; }
	FORCEINLINE const T& First() const { return ( *this )[0]; }
	FORCEINLINE T& Top() { return Last(); }
	FORCEINLINE const T& Top() const { return Last(); }

	FORCEINLINE T* GetData() noexcept { return Data; }
	FORCEINLINE const T* GetData() const noexcept { return Data; }

	//-------------------------------------------------------------------------
	// 追加（戻り値は追加した位置のインデックス）
	//-------------------------------------------------------------------------
	FORCEINLINE int32 Add( const T& Item ) { return Emplace( Item ); }
	FORCEINLINE int32 Add( T&& Item ) { return Emplace( MoveTemp( Item ) ); }

	template<typename... Args>
	FORCEINLINE int32 Emplace( Args&&... args )
	{
		EmplaceGetRef( Forward<Args>( args )... );
		return ArrayNum - 1;
	}

	template<typename... Args>
	T& EmplaceGetRef( Args&&... args );

	// 既に含まれていれば追加せず、そのインデックスを返す
	FORCEINLINE int32 AddUnique( const T& Item )
	{
		const int32 Index = Find( Item );
		return Index != INDEX_NONE ? Index : Add( Item );
	}

	// 値初期化した要素を Count 個追加し、最初のインデックスを返す
	FORCEINLINE int32 AddDefaulted( int32 Count = 1 )
	{
		const int32 Index = ArrayNum;
		SetNum( ArrayNum + Count );
		return Index;
	}

	// ゼロ埋めした要素を Count 個追加（トリビアル型向け）
	FORCEINLINE int32 AddZeroed( int32 Count = 1 )
	{
		static_assert( bIsTriviallyCopyable<T>, "AddZeroed requires a trivially copyable type" );
		const int32 Index = ArrayNum;
		GrowTo( ArrayNum + Count );
		FMemory::Memzero( Data + ArrayNum, sizeof( T ) * static_cast<usize>( Count ) );
		ArrayNum += Count;
		return Index;
	}

	// 未初期化の要素を Count 個追加（後で必ず上書きすること。トリビアル型向け）
	FORCEINLINE int32 AddUninitialized( int32 Count = 1 )
	{
		static_assert( bIsTriviallyCopyable<T>, "AddUninitialized requires a trivially copyable type" );
		const int32 Index = ArrayNum;
		GrowTo( ArrayNum + Count );
		ArrayNum += Count;
		return Index;
	}

	FORCEINLINE void Insert( const T& Item, int32 Index ) { EmplaceAt( Index, Item ); }
	FORCEINLINE void Insert( T&& Item, int32 Index ) { EmplaceAt( Index, MoveTemp( Item ) ); }

	template<typename... Args>
	FORCEINLINE void EmplaceAt( int32 Index, Args&&... args )
	{
		COLLECTION_CHECK( Index >= 0 && Index <= ArrayNum, "TArray::Insert index out of range" );

		// args が自分の要素を参照している可能性があるので先に値を作る
		T NewItem( Forward<Args>( args )... );
		OpenGap( Index, 1 );
		new ( Data + Index ) T( MoveTemp( NewItem ) );
	}

	FORCEINLINE void Append( const T* Ptr, int32 Count )
	{
		if ( Count <= 0 ) return;
		COLLECTION_CHECK( !( Ptr >= Data && Ptr < Data + ArrayNum ), "TArray::Append from itself" );
		ReserveForAppend( ArrayNum + Count );
		NMemoryOps::CopyConstructItems( Data + ArrayNum, Ptr, Count );
		ArrayNum += Count;
	}

	void Append( const TArray& Other );

	FORCEINLINE void Append( TArray&& Other )
	{
		if ( &Other == this ) { Append( static_cast<const TArray&>( Other ) ); return; }
		ReserveForAppend( ArrayNum + Other.ArrayNum );
		NMemoryOps::RelocateItems( Data + ArrayNum, Other.Data, Other.ArrayNum );
		ArrayNum += Other.ArrayNum;
		Other.ArrayNum = 0;
	}

	FORCEINLINE void Append( std::initializer_list<T> List ) { Append( List.begin(), static_cast<int32>( List.size() ) ); }

	TArray& operator+=( const TArray& Other ) { Append( Other ); return *this; }
	TArray& operator+=( TArray&& Other ) { Append( MoveTemp( Other ) ); return *this; }

	// スタック操作
	FORCEINLINE void Push( const T& Item ) { Add( Item ); }
	FORCEINLINE void Push( T&& Item ) { Add( MoveTemp( Item ) ); }
	FORCEINLINE T Pop()
	{
		COLLECTION_CHECK( !IsEmpty(), "TArray::Pop on empty array" );
		T Item = MoveTemp( Data[ArrayNum - 1] );
		NMemoryOps::DestructItem( Data + ArrayNum - 1 );
		--ArrayNum;
		return Item;
	}

	//-------------------------------------------------------------------------
	// 削除
	//-------------------------------------------------------------------------
	// Item と等しい要素をすべて削除し、削除数を返す（順序維持）
	FORCEINLINE int32 Remove( const T& Item )
	{
		const T Copy = Item;  // Item が自分の要素を指していても安全なように
		return RemoveAll( [&Copy]( const T& Element ) { return Element == Copy; } );
	}

	// Item と等しい最初の要素を削除（順序維持）
	FORCEINLINE bool RemoveSingle( const T& Item )
	{
		const int32 Index = Find( Item );
		if ( Index == INDEX_NONE ) return false;
		RemoveAt( Index );
		return true;
	}

	// 条件に合う要素をすべて削除し、削除数を返す（順序維持）
	template<typename Predicate>
	int32 RemoveAll( Predicate Pred );

	// Index から Count 個削除（順序維持）
	void RemoveAt( int32 Index, int32 Count = 1 );

	// Index の要素を末尾と入れ替えて削除（高速だが順序は崩れる）
	FORCEINLINE void RemoveAtSwap( int32 Index )
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "TArray::RemoveAtSwap index out of range" );
		if ( Index != ArrayNum - 1 ) Data[Index] = MoveTemp( Data[ArrayNum - 1] );
		NMemoryOps::DestructItem( Data + ArrayNum - 1 );
		--ArrayNum;
	}

	//-------------------------------------------------------------------------
	// 検索
	//-------------------------------------------------------------------------
	// 見つからなければ INDEX_NONE
	FORCEINLINE int32 Find( const T& Item ) const
	{
		for ( int32 i = 0; i < ArrayNum; ++i )
		{
			if ( Data[i] == Item ) return i;
		}
		return INDEX_NONE;
	}
	FORCEINLINE bool Find( const T& Item, int32& OutIndex ) const
	{
		OutIndex = Find( Item );
		return OutIndex != INDEX_NONE;
	}
	FORCEINLINE int32 FindLast( const T& Item ) const
	{
		for ( int32 i = ArrayNum - 1; i >= 0; --i )
		{
			if ( Data[i] == Item ) return i;
		}
		return INDEX_NONE;
	}

	FORCEINLINE bool Contains( const T& Item ) const { return Find( Item ) != INDEX_NONE; }

	template<typename Predicate>
	FORCEINLINE int32 IndexOfByPredicate( Predicate Pred ) const
	{
		for ( int32 i = 0; i < ArrayNum; ++i )
		{
			if ( Pred( Data[i] ) ) return i;
		}
		return INDEX_NONE;
	}

	// 条件に合う最初の要素へのポインタ（無ければ nullptr）
	template<typename Predicate>
	FORCEINLINE T* FindByPredicate( Predicate Pred )
	{
		const int32 Index = IndexOfByPredicate( Pred );
		return Index != INDEX_NONE ? Data + Index : nullptr;
	}
	template<typename Predicate>
	FORCEINLINE const T* FindByPredicate( Predicate Pred ) const
	{
		const int32 Index = IndexOfByPredicate( Pred );
		return Index != INDEX_NONE ? Data + Index : nullptr;
	}

	template<typename Predicate>
	FORCEINLINE bool ContainsByPredicate( Predicate Pred ) const { return IndexOfByPredicate( Pred ) != INDEX_NONE; }

	template<typename Predicate>
	FORCEINLINE bool AllOf( Predicate Pred ) const
	{
		for ( int32 i = 0; i < ArrayNum; ++i )
		{
			if ( !Pred( Data[i] ) ) return false;
		}
		return true;
	}

	template<typename Predicate>
	FORCEINLINE int32 CountByPredicate( Predicate Pred ) const
	{
		int32 Count = 0;
		for ( int32 i = 0; i < ArrayNum; ++i )
		{
			if ( Pred( Data[i] ) ) ++Count;
		}
		return Count;
	}

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	// 条件に合う要素だけの新しい配列
	template<typename Predicate>
	FORCEINLINE TArray FilterByPredicate( Predicate Pred ) const
	{
		TArray Result;
		for ( int32 i = 0; i < ArrayNum; ++i )
		{
			if ( Pred( Data[i] ) ) Result.Add( Data[i] );
		}
		return Result;
	}

	// 各要素に Func を適用した新しい配列
	template<typename Func>
	FORCEINLINE auto Map( Func&& Fn ) const
	{
		using U = TDecayT<TInvokeResultT<Func, const T&>>;
		TArray<U> Result;
		Result.Reserve( ArrayNum );
		for ( int32 i = 0; i < ArrayNum; ++i ) Result.Add( Fn( Data[i] ) );
		return Result;
	}

	//-------------------------------------------------------------------------
	// 並べ替え
	//-------------------------------------------------------------------------
	FORCEINLINE void Sort() { NAlgo::Sort( Data, ArrayNum ); }
	template<typename Predicate>
	FORCEINLINE void Sort( Predicate Pred ) { NAlgo::Sort( Data, ArrayNum, Pred ); }

	FORCEINLINE void StableSort() { NAlgo::StableSort( Data, ArrayNum ); }
	template<typename Predicate>
	FORCEINLINE void StableSort( Predicate Pred ) { NAlgo::StableSort( Data, ArrayNum, Pred ); }

	FORCEINLINE void Reverse() { NAlgo::Reverse( Data, ArrayNum ); }

	FORCEINLINE void Swap( int32 IndexA, int32 IndexB )
	{
		COLLECTION_CHECK( IsValidIndex( IndexA ) && IsValidIndex( IndexB ), "TArray::Swap index out of range" );
		NAlgo::Swap( Data[IndexA], Data[IndexB] );
	}

	//-------------------------------------------------------------------------
	// 比較・イテレータ
	//-------------------------------------------------------------------------
	friend FORCEINLINE bool operator==( const TArray& A, const TArray& B )
	{
		if ( A.ArrayNum != B.ArrayNum ) return false;
		for ( int32 i = 0; i < A.ArrayNum; ++i )
		{
			if ( !( A.Data[i] == B.Data[i] ) ) return false;
		}
		return true;
	}
	friend bool operator!=( const TArray& A, const TArray& B ) { return !( A == B ); }

	FORCEINLINE T* begin() noexcept { return Data; }
	FORCEINLINE T* end() noexcept { return ArrayNum > 0 ? Data + ArrayNum : Data; }
	FORCEINLINE const T* begin() const noexcept { return Data; }
	FORCEINLINE const T* end() const noexcept { return ArrayNum > 0 ? Data + ArrayNum : Data; }
	// 先頭から走査する。削除はこの反復子の RemoveCurrent 系だけで行う。
	FORCEINLINE Iterator CreateIterator() { return Iterator( *this ); }
	// const 参照を返す反復子を先頭から作る。
	FORCEINLINE ConstIterator CreateConstIterator() const { return ConstIterator( *this ); }

private:
	// 追記のために、容量を Required 以上にする。空の配列（作った直後のコピーなど）には必要な数ちょうど、すでに要素がある配列には少し余分に確保する
	FORCEINLINE void ReserveForAppend( int32 Required )
	{
		if ( ArrayMax == 0 ) Reserve( Required );
		else GrowTo( Required );
	}

	// 容量を Required 以上にする。足りないときは、再確保の回数を抑えるため、少し余分に確保する（Reserve は指定した数ちょうど）
	FORCEINLINE void GrowTo( int32 Required )
	{
		if ( Required > ArrayMax ) Reallocate( CalculateGrowth( Required ) );
	}

	FORCEINLINE static int32 CalculateGrowth( int32 Required )
	{
		// 1.5 倍 + 少し（小さい配列でも再確保回数を抑える）
		return Required + Required / 2 + 4;
	}

	void Reallocate( int32 NewMax );

	// [Index, Index + Count) を未初期化の空きにして後ろへずらす
	void OpenGap( int32 Index, int32 Count );

	T*    Data     = nullptr;
	int32 ArrayNum = 0;
	int32 ArrayMax = 0;
};

#include "Array.inl"

#endif
