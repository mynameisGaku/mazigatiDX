// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_SET_H
#define FRAMEWORK_CORE_COLLECTION_SET_H
#include "Library/Core/Types/CompilerMacros.h"
#include <initializer_list>
#include <utility>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/HashTable.h"

//=============================================================================
// TSet<T>
//   重複なしの集合（ハッシュテーブル）。順序は不定。
//   要素型には operator== と GetTypeHash が必要（Hash.h 参照）。
//
//   TSet<int32> Visited;
//   if ( Visited.Add( Id ) ) { ...初めて追加された... }
//   TSet<int32> Both = A.Intersect( B );
//=============================================================================
template<typename T, typename Hasher = TDefaultHasher<T>>
class TSet
{
	struct FKeyFuncs
	{
		FORCEINLINE static const T& GetKey( const T& Element ) { return Element; }
	};
	using TableType = THashTable<T, T, FKeyFuncs, Hasher>;

public:
	using ElementType    = T;
	// 値を書き換えずに走査し、非 const の場合だけ削除できる反復子。
	template<bool bConst>
	class TIterator
	{
		// 削除可否に対応するテーブルの反復子。
		using SourceIterator = std::conditional_t<bConst, typename TableType::const_iterator, typename TableType::iterator>;
	public:
		// 既存の走査位置を集合用に包む。
		FORCEINLINE explicit TIterator( SourceIterator InCurrent ) : Current( InCurrent ) {}
		// ハッシュを保つため、現在の値は参照専用。
		FORCEINLINE const T& operator*() const { return *Current; }
		// 現在の値を参照専用のポインタで返す。
		FORCEINLINE const T* operator->() const { return Current.operator->(); }
		// 次の値へ進む。
		FORCEINLINE TIterator& operator++() { ++Current; return *this; }
		// 終端でなければ true。
		FORCEINLINE explicit operator bool() const { return static_cast<bool>( Current ); }
		// 同じ集合の走査位置を比較する。
		FORCEINLINE bool operator==( const TIterator& Other ) const { return Current == Other.Current; }
		// 同じ集合の走査位置を比較する。
		FORCEINLINE bool operator!=( const TIterator& Other ) const { return Current != Other.Current; }
		// 集合ではキーと値は同じ要素を参照する。
		FORCEINLINE const T& Key() const { return *Current; }
		// 現在の値を参照する。変更はできない。
		FORCEINLINE const T& Value() const { return *Current; }
		// 内部テーブルの位置を返す。
		FORCEINLINE int32 GetIndex() const { return Current.GetIndex(); }
		// 現在の値を削除する。次の ++ まで参照しない。
		FORCEINLINE void RemoveCurrent() { Current.RemoveCurrent(); }
	private:
		// 集合が所有するテーブル上の位置。
		SourceIterator Current;
	};
	// 現在の要素を削除できる反復子。
	using iterator = TIterator<false>;
	// 値を参照する反復子。
	using const_iterator = TIterator<true>;

	TSet() = default;
	FORCEINLINE TSet( std::initializer_list<T> List )
	{
		Reserve( static_cast<int32>( List.size() ) );
		for ( const T& Item : List ) Add( Item );
	}
	FORCEINLINE explicit TSet( const TArray<T>& Array )
	{
		Reserve( Array.Num() );
		for ( const T& Item : Array ) Add( Item );
	}

	//-------------------------------------------------------------------------
	// サイズ
	//-------------------------------------------------------------------------
	FORCEINLINE int32 Num() const noexcept { return Table.Num(); }
	FORCEINLINE bool IsEmpty() const noexcept { return Table.IsEmpty(); }
	FORCEINLINE void Empty() { Table.Empty(); }
	FORCEINLINE void Reset() { Table.Reset(); }
	FORCEINLINE void Reserve( int32 Count ) { Table.Reserve( Count ); }

	//-------------------------------------------------------------------------
	// 追加・削除
	//-------------------------------------------------------------------------
	// 新しく追加されたら true（既にあれば false）
	FORCEINLINE bool Add( const T& Item )
	{
		bool bAdded = false;
		Table.FindOrAdd( Item, [&Item]() { return T( Item ); }, bAdded );
		return bAdded;
	}
	FORCEINLINE bool Add( T&& Item )
	{
		bool bAdded = false;
		Table.FindOrAdd( Item, [&Item]() { return T( std::move( Item ) ); }, bAdded );
		return bAdded;
	}

	template<typename... Args>
	FORCEINLINE bool Emplace( Args&&... args ) { return Add( T( std::forward<Args>( args )... ) ); }

	FORCEINLINE void Append( const TArray<T>& Items ) { for ( const T& Item : Items ) Add( Item ); }
	FORCEINLINE void Append( const TSet& Other ) { for ( const T& Item : Other ) Add( Item ); }

	// 削除数 (0 or 1)
	FORCEINLINE int32 Remove( const T& Item ) { return Table.Remove( Item ) ? 1 : 0; }

	template<typename Predicate>
	FORCEINLINE int32 RemoveAll( Predicate Pred ) { return Table.RemoveIf( Pred ); }

	//-------------------------------------------------------------------------
	// 検索
	//-------------------------------------------------------------------------
	FORCEINLINE bool Contains( const T& Item ) const { return Table.FindIndex( Item ) != INDEX_NONE; }

	// 要素へのポインタ（無ければ nullptr）
	FORCEINLINE const T* Find( const T& Item ) const { return Table.Find( Item ); }

	//-------------------------------------------------------------------------
	// 集合演算
	//-------------------------------------------------------------------------
	// 和集合
	FORCEINLINE TSet Union( const TSet& Other ) const
	{
		TSet Result = *this;
		Result.Append( Other );
		return Result;
	}

	// 積集合
	FORCEINLINE TSet Intersect( const TSet& Other ) const
	{
		const TSet& Smaller = Num() <= Other.Num() ? *this : Other;
		const TSet& Larger  = Num() <= Other.Num() ? Other : *this;
		TSet Result;
		for ( const T& Item : Smaller )
		{
			if ( Larger.Contains( Item ) ) Result.Add( Item );
		}
		return Result;
	}

	// 差集合（this にあって Other にないもの）
	FORCEINLINE TSet Difference( const TSet& Other ) const
	{
		TSet Result;
		for ( const T& Item : *this )
		{
			if ( !Other.Contains( Item ) ) Result.Add( Item );
		}
		return Result;
	}

	// Other のすべてを含むか（部分集合判定）
	FORCEINLINE bool Includes( const TSet& Other ) const
	{
		for ( const T& Item : Other )
		{
			if ( !Contains( Item ) ) return false;
		}
		return true;
	}

	FORCEINLINE TArray<T> Array() const
	{
		TArray<T> Result;
		Result.Reserve( Num() );
		for ( const T& Item : *this ) Result.Add( Item );
		return Result;
	}

	friend bool operator==( const TSet& A, const TSet& B ) { return A.Num() == B.Num() && A.Includes( B ); }
	friend bool operator!=( const TSet& A, const TSet& B ) { return !( A == B ); }

	// 要素は変更不可（ハッシュが変わるため）
	FORCEINLINE const_iterator begin() const { return const_iterator( Table.begin() ); }
	FORCEINLINE const_iterator end() const { return const_iterator( Table.end() ); }
	// 要素を削除しながら走査する反復子を作る。
	FORCEINLINE iterator CreateIterator() { return iterator( Table.CreateIterator() ); }
	// 要素を変更できない反復子を作る。
	FORCEINLINE const_iterator CreateConstIterator() const { return begin(); }

private:
	TableType Table;
};

#endif
