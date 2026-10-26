// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_MAP_H
#define FRAMEWORK_CORE_COLLECTION_MAP_H
#include "Library/Core/Types/CompilerMacros.h"
#include <initializer_list>
#include <utility>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/HashTable.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// TMap<K, V>
//   ハッシュマップ。順序は不定。キー型には operator== と GetTypeHash が必要。
//
//   TMap<FString, int32> Scores;
//   Scores.Add( "Alice", 10 );
//   if ( int32* Score = Scores.Find( "Alice" ) ) *Score += 5;
//   int32 Bob = Scores.FindRef( "Bob" );            // 無ければ 0
//   for ( auto& [Name, Score] : Scores ) ...
//
//   ※ operator[] は UE と同じく「存在するキー専用」（無ければ assert）。
//     無ければ追加したい場合は FindOrAdd を使う。
//   ※ 追加で再ハッシュが起きると、Find で得たポインタは無効になる
//=============================================================================
template<typename K, typename V, typename Hasher = TDefaultHasher<K>>
class TMap
{
	using PairType = TPair<K, V>;

	struct FKeyFuncs
	{
		FORCEINLINE static const K& GetKey( const PairType& Pair ) { return Pair.Key; }
	};
	using TableType = THashTable<PairType, K, FKeyFuncs, Hasher>;

public:
	using KeyType        = K;
	using ValueType      = V;
	using ElementType    = PairType;
	// キーは参照専用、値は const に応じて書き換えられる反復子。
	template<bool bConst>
	class TIterator
	{
		// 内部テーブルの対応する反復子。
		using SourceIterator = std::conditional_t<bConst, typename TableType::const_iterator, typename TableType::iterator>;
	public:
		// 既存の走査位置をマップ用に包む。
		FORCEINLINE explicit TIterator( SourceIterator InCurrent ) : Current( InCurrent ) {}
		// 既存の範囲 for と同じ組を返す。Key は書き換えないこと。
		FORCEINLINE decltype(auto) operator*() const { return *Current; }
		// 現在の組を指す。Key は書き換えないこと。
		FORCEINLINE auto operator->() const { return Current.operator->(); }
		// 次の組へ進む。
		FORCEINLINE TIterator& operator++() { ++Current; return *this; }
		// 終端でなければ true。
		FORCEINLINE explicit operator bool() const { return static_cast<bool>( Current ); }
		// 同じテーブルの走査位置を比較する。
		FORCEINLINE bool operator==( const TIterator& Other ) const { return Current == Other.Current; }
		// 同じテーブルの走査位置を比較する。
		FORCEINLINE bool operator!=( const TIterator& Other ) const { return Current != Other.Current; }
		// 現在のキーを参照する。ハッシュを保つため変更できない。
		FORCEINLINE const K& Key() const { return Current->Key; }
		// 現在の値を const に応じた参照で返す。
		FORCEINLINE decltype(auto) Value() const { return ( Current->Value ); }
		// 内部テーブルの位置を返す。
		FORCEINLINE int32 GetIndex() const { return Current.GetIndex(); }
		// 現在の組を削除する。次の ++ まで参照しない。
		FORCEINLINE void RemoveCurrent() { Current.RemoveCurrent(); }
	private:
		// マップが所有するテーブル上の位置。
		SourceIterator Current;
	};
	// 値を書き換え、現在要素を削除できる反復子。
	using iterator = TIterator<false>;
	// キーと値を参照する反復子。
	using const_iterator = TIterator<true>;

	TMap() = default;
	FORCEINLINE TMap( std::initializer_list<PairType> List )
	{
		Reserve( static_cast<int32>( List.size() ) );
		for ( const PairType& Pair : List ) Add( Pair.Key, Pair.Value );
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
	// 追加（既にあれば上書き）。追加・上書きした値への参照を返す
	//-------------------------------------------------------------------------
	FORCEINLINE V& Add( const K& Key, const V& Value ) { return AddImpl( Key, Value ); }
	FORCEINLINE V& Add( const K& Key, V&& Value ) { return AddImpl( Key, MoveTemp( Value ) ); }
	FORCEINLINE V& Add( K&& Key, const V& Value ) { return AddImpl( MoveTemp( Key ), Value ); }
	FORCEINLINE V& Add( K&& Key, V&& Value ) { return AddImpl( MoveTemp( Key ), MoveTemp( Value ) ); }

	template<typename... Args>
	FORCEINLINE V& Emplace( const K& Key, Args&&... args ) { return AddImpl( Key, V( Forward<Args>( args )... ) ); }

	// あればその値、無ければ値初期化して追加
	FORCEINLINE V& FindOrAdd( const K& Key )
	{
		bool bAdded = false;
		return Table.FindOrAdd( Key, [&Key]() { return PairType { Key, V() }; }, bAdded ).Value;
	}
	FORCEINLINE V& FindOrAdd( const K& Key, const V& Default )
	{
		bool bAdded = false;
		return Table.FindOrAdd( Key, [&Key, &Default]() { return PairType { Key, Default }; }, bAdded ).Value;
	}

	//-------------------------------------------------------------------------
	// 検索
	//-------------------------------------------------------------------------
	// 値へのポインタ（無ければ nullptr）
	FORCEINLINE V* Find( const K& Key )
	{
		PairType* Pair = Table.Find( Key );
		return Pair ? &Pair->Value : nullptr;
	}
	FORCEINLINE const V* Find( const K& Key ) const
	{
		const PairType* Pair = Table.Find( Key );
		return Pair ? &Pair->Value : nullptr;
	}

	// 値のコピー（無ければ Default）
	FORCEINLINE V FindRef( const K& Key, const V& Default = V() ) const
	{
		const V* Value = Find( Key );
		return Value ? *Value : Default;
	}

	// 存在が確実なキーの値（無ければ assert）
	FORCEINLINE V& FindChecked( const K& Key )
	{
		V* Value = Find( Key );
		COLLECTION_CHECK( Value != nullptr, "TMap::FindChecked key not found" );
		return *Value;
	}
	FORCEINLINE const V& FindChecked( const K& Key ) const
	{
		const V* Value = Find( Key );
		COLLECTION_CHECK( Value != nullptr, "TMap::FindChecked key not found" );
		return *Value;
	}

	V& operator[]( const K& Key ) { return FindChecked( Key ); }
	const V& operator[]( const K& Key ) const { return FindChecked( Key ); }

	FORCEINLINE bool Contains( const K& Key ) const { return Table.FindIndex( Key ) != INDEX_NONE; }

	// 値からキーを逆引き（線形探索。無ければ nullptr）
	FORCEINLINE const K* FindKey( const V& Value ) const
	{
		for ( const PairType& Pair : Table )
		{
			if ( Pair.Value == Value ) return &Pair.Key;
		}
		return nullptr;
	}

	//-------------------------------------------------------------------------
	// 削除
	//-------------------------------------------------------------------------
	// 削除数 (0 or 1)
	FORCEINLINE int32 Remove( const K& Key ) { return Table.Remove( Key ) ? 1 : 0; }

	// 削除して値を取り出す
	FORCEINLINE bool RemoveAndCopyValue( const K& Key, V& OutValue )
	{
		const int32 Index = Table.FindIndex( Key );
		if ( Index == INDEX_NONE ) return false;
		OutValue = MoveTemp( Table.GetAt( Index ).Value );
		Table.RemoveAt( Index );
		return true;
	}

	// 条件 Pred( Key, Value ) に合う要素を削除し、削除数を返す
	template<typename Predicate>
	FORCEINLINE int32 RemoveAll( Predicate Pred )
	{
		return Table.RemoveIf( [&Pred]( PairType& Pair ) { return Pred( Pair.Key, Pair.Value ); } );
	}

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	FORCEINLINE TArray<K> GetKeys() const
	{
		TArray<K> Keys;
		Keys.Reserve( Num() );
		for ( const PairType& Pair : Table ) Keys.Add( Pair.Key );
		return Keys;
	}

	FORCEINLINE TArray<V> GetValues() const
	{
		TArray<V> Values;
		Values.Reserve( Num() );
		for ( const PairType& Pair : Table ) Values.Add( Pair.Value );
		return Values;
	}

	// 条件 Pred( Key, Value ) に合う要素だけの新しいマップ
	template<typename Predicate>
	FORCEINLINE TMap FilterByPredicate( Predicate Pred ) const
	{
		TMap Result;
		for ( const PairType& Pair : Table )
		{
			if ( Pred( Pair.Key, Pair.Value ) ) Result.Add( Pair.Key, Pair.Value );
		}
		return Result;
	}

	friend FORCEINLINE bool operator==( const TMap& A, const TMap& B )
	{
		if ( A.Num() != B.Num() ) return false;
		for ( const PairType& Pair : A.Table )
		{
			const V* Other = B.Find( Pair.Key );
			if ( !Other || !( *Other == Pair.Value ) ) return false;
		}
		return true;
	}
	friend bool operator!=( const TMap& A, const TMap& B ) { return !( A == B ); }

	// 要素は TPair<K, V>（Key を書き換えないこと）
	FORCEINLINE iterator begin() { return iterator( Table.begin() ); }
	FORCEINLINE iterator end() { return iterator( Table.end() ); }
	FORCEINLINE const_iterator begin() const { return const_iterator( Table.begin() ); }
	FORCEINLINE const_iterator end() const { return const_iterator( Table.end() ); }
	// キーと値を参照し、現在要素を削除できる反復子を作る。
	FORCEINLINE iterator CreateIterator() { return begin(); }
	// キーと値を変更できない反復子を作る。
	FORCEINLINE const_iterator CreateConstIterator() const { return begin(); }

private:
	template<typename KArg, typename VArg>
	V& AddImpl( KArg&& Key, VArg&& Value );

	TableType Table;
};

#include "Map.inl"

#endif
