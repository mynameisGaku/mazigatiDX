// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_HASHTABLE_H
#define FRAMEWORK_CORE_COLLECTION_HASHTABLE_H
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Collection/Hash.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/TypeTraits.h"

//=============================================================================
// THashTable（TSet / TMap の内部実装）
//   オープンアドレス法（線形探索）のハッシュテーブル。
//   削除時は後続要素を詰める（backward shift）ので墓標を残さない。
//
//   KeyFuncs::GetKey( const ElementType& ) -> const KeyType&
//   Hasher( const KeyType& ) -> uint32
//
//   ※ 追加で再ハッシュが起きると、要素へのポインタ・参照は無効になる
//=============================================================================
template<typename ElementType, typename KeyType, typename KeyFuncs, typename Hasher>
class THashTable
{
	// 使用中のスロットを区別するビット。
	static constexpr uint32 OccupiedBit = 0x80000000u;
	// 最初に確保するスロット数。
	static constexpr int32 MinCapacity = 8;

public:
	//-------------------------------------------------------------------------
	// イテレータ
	//   走査は空きスロットの次から 1 周する。こうするとクラスタ（連続した使用中スロット）が
	//   走査の切れ目をまたがないので、RemoveCurrent で後続の要素が詰められても
	//   「まだ見ていない要素が今の位置か後ろに来る」だけになり、取りこぼしも二重訪問も起きない。
	//
	//   for ( auto It = Table.CreateIterator(); It; ++It )
	//   {
	//       if ( ShouldRemove( *It ) ) It.RemoveCurrent();   // 削除後は次の ++ まで *It を使わない
	//   }
	//-------------------------------------------------------------------------
	template<bool bConst>
	class TIterator
	{
		using TableType = TConditionalT<bConst, const THashTable, THashTable>;
		using RefType   = TConditionalT<bConst, const ElementType&, ElementType&>;
		using PtrType   = TConditionalT<bConst, const ElementType*, ElementType*>;

	public:
		// bEnd = true で終端（範囲 for の end() 用）
		FORCEINLINE TIterator( TableType* InTable, bool bEnd ) : Table( InTable ), Start( bEnd ? 0 : InTable->FindScanStart() ), Step( bEnd ? InTable->Capacity : 0 )
		{
			SkipEmpty();
		}

		// 現在要素を返す。終端または削除直後は不正。
		FORCEINLINE RefType operator*() const
		{
			COLLECTION_CHECK( static_cast<bool>( *this ) && !bCurrentRemoved, "THashTable iterator has no current element" );
			return Table->Elements[GetIndex()];
		}
		// 現在要素のアドレスを返す。終端または削除直後は不正。
		FORCEINLINE PtrType operator->() const { return &operator*(); }

		FORCEINLINE TIterator& operator++()
		{
			// 削除した位置には後続の要素が詰められている可能性があるので、進めずに再確認する
			if ( bCurrentRemoved ) bCurrentRemoved = false;
			else if ( Step < Table->Capacity ) ++Step;
			SkipEmpty();
			return *this;
		}

		FORCEINLINE explicit operator bool() const { return Step < Table->Capacity; }
		// 別のテーブルに属する位置は等しくない。
		FORCEINLINE bool operator==( const TIterator& Other ) const { return Table == Other.Table && GetIndex() == Other.GetIndex(); }
		// 現在のスロットまたは終端が異なるかを返す。
		FORCEINLINE bool operator!=( const TIterator& Other ) const { return !( *this == Other ); }

		// テーブル内の位置（GetAt / RemoveAt に渡せる）
		FORCEINLINE int32 GetIndex() const { return Step < Table->Capacity ? ( Start + Step ) & ( Table->Capacity - 1 ) : INDEX_NONE; }

		// 現在の要素を削除する。次の ++ で次の要素へ進む
		FORCEINLINE void RemoveCurrent()
		{
			static_assert( !bConst, "RemoveCurrent cannot be used with a const iterator" );
			COLLECTION_CHECK( static_cast<bool>( *this ) && !bCurrentRemoved, "THashTable iterator has no current element" );
			Table->RemoveAt( GetIndex() );
			bCurrentRemoved = true;
		}

	private:
		FORCEINLINE void SkipEmpty()
		{
			while ( Step < Table->Capacity && Table->Hashes[GetIndex()] == 0 ) ++Step;
		}

		// 要素を所有するテーブル。
		TableType* Table;
		// 作成時の空きスロット。走査の切れ目を固定する。
		int32 Start;
		// 開始位置から進んだスロット数。
		int32 Step;
		// 削除位置を次の ++ で再確認する。
		bool bCurrentRemoved = false;
	};

	using iterator       = TIterator<false>;
	using const_iterator = TIterator<true>;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	THashTable() = default;

	FORCEINLINE THashTable( const THashTable& Other ) { CopyFrom( Other ); }

	THashTable( THashTable&& Other ) noexcept
		: Hashes( Other.Hashes ), Elements( Other.Elements ), Capacity( Other.Capacity ), Count( Other.Count )
	{
		Other.Hashes   = nullptr;
		Other.Elements = nullptr;
		Other.Capacity = 0;
		Other.Count    = 0;
	}

	FORCEINLINE ~THashTable() { Empty(); }

	FORCEINLINE THashTable& operator=( const THashTable& Other )
	{
		if ( this != &Other )
		{
			Empty();
			CopyFrom( Other );
		}
		return *this;
	}

	THashTable& operator=( THashTable&& Other ) noexcept;

	//-------------------------------------------------------------------------
	// サイズ
	//-------------------------------------------------------------------------
	FORCEINLINE int32 Num() const noexcept { return Count; }
	FORCEINLINE bool IsEmpty() const noexcept { return Count == 0; }

	// 全要素を削除してメモリも解放
	FORCEINLINE void Empty()
	{
		Reset();
		FMemory::Free( Hashes );
		FMemory::Free( Elements );
		Hashes   = nullptr;
		Elements = nullptr;
		Capacity = 0;
	}

	// 全要素を削除（メモリは保持）
	void Reset();

	// ElementCount 個を再ハッシュなしで格納できるようにする
	FORCEINLINE void Reserve( int32 ElementCount )
	{
		const int32 Required = CapacityFor( ElementCount );
		if ( Required > Capacity ) Rehash( Required );
	}

	//-------------------------------------------------------------------------
	// 検索
	//-------------------------------------------------------------------------
	int32 FindIndex( const KeyType& Key ) const;

	FORCEINLINE ElementType* Find( const KeyType& Key )
	{
		const int32 Index = FindIndex( Key );
		return Index != INDEX_NONE ? Elements + Index : nullptr;
	}
	FORCEINLINE const ElementType* Find( const KeyType& Key ) const
	{
		const int32 Index = FindIndex( Key );
		return Index != INDEX_NONE ? Elements + Index : nullptr;
	}

	FORCEINLINE ElementType& GetAt( int32 Index ) { return Elements[Index]; }
	FORCEINLINE const ElementType& GetAt( int32 Index ) const { return Elements[Index]; }

	//-------------------------------------------------------------------------
	// 追加
	//   Key が既にあればその要素、無ければ MakeElement() で構築した要素を返す。
	//   bOutAdded に新規追加かどうかが入る。
	//-------------------------------------------------------------------------
	template<typename Constructor>
	ElementType& FindOrAdd( const KeyType& Key, Constructor&& MakeElement, bool& bOutAdded );

	//-------------------------------------------------------------------------
	// 削除
	//-------------------------------------------------------------------------
	FORCEINLINE bool Remove( const KeyType& Key )
	{
		const int32 Index = FindIndex( Key );
		if ( Index == INDEX_NONE ) return false;
		RemoveAt( Index );
		return true;
	}

	// Index の要素を削除し、後続の要素を詰める
	void RemoveAt( int32 Index );

	// 条件 Pred( Element ) に合う要素をすべて削除し、削除数を返す
	template<typename Predicate>
	int32 RemoveIf( Predicate Pred );

	//-------------------------------------------------------------------------
	// イテレータ
	//-------------------------------------------------------------------------
	FORCEINLINE iterator begin() { return iterator( this, false ); }
	FORCEINLINE iterator end() { return iterator( this, true ); }
	FORCEINLINE const_iterator begin() const { return const_iterator( this, false ); }
	FORCEINLINE const_iterator end() const { return const_iterator( this, true ); }

	FORCEINLINE iterator CreateIterator() { return iterator( this, false ); }
	FORCEINLINE const_iterator CreateConstIterator() const { return const_iterator( this, false ); }

private:
	FORCEINLINE static uint32 HashKey( const KeyType& Key ) { return Hasher()( Key ) | OccupiedBit; }

	// 走査の開始位置（空きスロット）。空きは必ずある（満杯になる前に拡張する）
	FORCEINLINE int32 FindScanStart() const
	{
		for ( int32 i = 0; i < Capacity; ++i )
		{
			if ( Hashes[i] == 0 ) return i;
		}
		return 0;
	}

	// 負荷率 3/4 以下に収まる 2 のべき乗の容量を返す。
	FORCEINLINE static int32 CapacityFor( int32 ElementCount )
	{
		if ( ElementCount <= 0 ) return 0;
		int32 Result = MinCapacity;
		while ( Result * 3 / 4 < ElementCount ) Result *= 2;
		return Result;
	}

	FORCEINLINE int32 FindEmptySlot( uint32 Hash ) const
	{
		const int32 Mask = Capacity - 1;
		int32 i = static_cast<int32>( Hash ) & Mask;
		while ( Hashes[i] != 0 ) i = ( i + 1 ) & Mask;
		return i;
	}

	void Rehash( int32 NewCapacity );

	void CopyFrom( const THashTable& Other );

	uint32*      Hashes   = nullptr;  // 0 = 空き。使用中は最上位ビットが立つ
	ElementType* Elements = nullptr;
	int32        Capacity = 0;        // 2 のべき乗
	int32        Count    = 0;
};

#include "HashTable.inl"

#endif
