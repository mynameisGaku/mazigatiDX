#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <initializer_list>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeTraits.h"

//=============================================================================
// TArrayView<T>
//   連続した要素への所有権を持たない参照（C++20 の std::span 相当）。
//   TArray / 生配列 / ポインタ+個数 / 初期化子リスト のどれからでも作れるので、
//   「配列を読むだけ」の関数の引数に使うと便利。
//
//   void Draw( TArrayView<const FVector3> Points );
//   Draw( MyArray );   Draw( MyCArray );   Draw( { a, b, c } );
//
//   ※ 参照先より長生きさせないこと
//=============================================================================
template<typename T>
class TArrayView
{
	using MutableT = TRemoveConstT<T>;

public:
	using ElementType = T;

	constexpr TArrayView() = default;
	FORCEINLINE constexpr TArrayView( T* InData, int32 InNum ) : DataPtr( InData ), Count( InNum ) {}

	template<usize N>
	FORCEINLINE constexpr TArrayView( T ( &Array )[N] ) : DataPtr( Array ), Count( static_cast<int32>( N ) ) {}

	FORCEINLINE TArrayView( TArray<MutableT>& Array ) : DataPtr( Array.GetData() ), Count( Array.Num() ) {}

	template<typename U = T, typename = TEnableIfT<bIsConst<U>>>
	FORCEINLINE TArrayView( const TArray<MutableT>& Array ) : DataPtr( Array.GetData() ), Count( Array.Num() ) {}

	// 一時的な初期化子リスト（呼び出し式の間だけ有効）
	template<typename U = T, typename = TEnableIfT<bIsConst<U>>>
	FORCEINLINE TArrayView( std::initializer_list<MutableT> List ) : DataPtr( List.begin() ), Count( static_cast<int32>( List.size() ) ) {}

	// TArrayView<T> -> TArrayView<const T>
	template<typename U, typename = TEnableIfT<bIsSame<const U, T> && !bIsSame<U, T>>>
	FORCEINLINE constexpr TArrayView( const TArrayView<U>& Other ) : DataPtr( Other.GetData() ), Count( Other.Num() ) {}

	FORCEINLINE constexpr int32 Num() const noexcept { return Count; }
	FORCEINLINE constexpr bool IsEmpty() const noexcept { return Count == 0; }
	FORCEINLINE constexpr bool IsValidIndex( int32 Index ) const noexcept { return Index >= 0 && Index < Count; }
	FORCEINLINE constexpr T* GetData() const noexcept { return DataPtr; }

	FORCEINLINE T& operator[]( int32 Index ) const
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "TArrayView index out of range" );
		return DataPtr[Index];
	}
	FORCEINLINE T& Last( int32 IndexFromEnd = 0 ) const { return ( *this )[Count - 1 - IndexFromEnd]; }

	// [Start, Start + InNum) の部分ビュー
	FORCEINLINE TArrayView Slice( int32 Start, int32 InNum ) const
	{
		COLLECTION_CHECK( Start >= 0 && InNum >= 0 && Start + InNum <= Count, "TArrayView::Slice out of range" );
		return TArrayView( DataPtr + Start, InNum );
	}

	FORCEINLINE int32 Find( const MutableT& Item ) const
	{
		for ( int32 i = 0; i < Count; ++i )
		{
			if ( DataPtr[i] == Item ) return i;
		}
		return INDEX_NONE;
	}
	FORCEINLINE bool Contains( const MutableT& Item ) const { return Find( Item ) != INDEX_NONE; }

	template<typename Predicate>
	FORCEINLINE int32 IndexOfByPredicate( Predicate Pred ) const
	{
		for ( int32 i = 0; i < Count; ++i )
		{
			if ( Pred( DataPtr[i] ) ) return i;
		}
		return INDEX_NONE;
	}

	FORCEINLINE TArray<MutableT> ToArray() const { return TArray<MutableT>( DataPtr, Count ); }

	FORCEINLINE constexpr T* begin() const noexcept { return DataPtr; }
	FORCEINLINE constexpr T* end() const noexcept { return DataPtr + Count; }

private:
	T*    DataPtr = nullptr;
	int32 Count   = 0;
};
