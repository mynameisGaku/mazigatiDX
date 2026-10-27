// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// TTuple<Ts...>
//   型の違う値を、決まった並びでまとめて持つ。std::tuple の代わり。標準ライブラリに依存しない。
//     std::tuple<A, B>                -> TTuple<A, B>
//     std::get<I>( T )                -> NTuple::Get<I>( T )
//     std::tuple_size_v<T>            -> TupleSize<T>
//     std::tuple_element_t<I, T>      -> TTupleElementT<I, T>
//     std::apply( F, T )              -> NTuple::Apply( F, T )
//     std::make_tuple( ... )          -> NTuple::Make( ... )
//
//   TTuple<int32, float32, FString> Row;                     // 引数なしで作ると、値初期化（0 / nullptr / 空）になる
//   TTuple<int32, float32> Pair( 1, 2.5f );                  // 値を渡して作る（個数は型の数と同じ）
//   NTuple::Get<0>( Pair ) = 5;                              // 参照が返るので、読むことも書くこともできる
//   const int32 Sum = NTuple::Apply( []( int32 A, float32 B ) { return A + static_cast<int32>( B ); }, Pair );
//
//   コピー・移動・代入は、要素ごとに行う（移動しかできない要素を持てば、TTuple も移動しかできない）。
//   要素に参照を持てる（TTuple<int32&>）。比較と swap は持たない。
//=============================================================================
template<typename... Ts>
class TTuple;

namespace NTupleDetail
{
	// 型の並び（添字での取り出しと、個数の比較に使う）。
	template<typename... Ts>
	struct TTypeList {};

	// 型の並び Ts... の I 番目。
	template<usize I, typename... Ts>
	struct TTypeAt;

	template<typename T, typename... Ts>
	struct TTypeAt<0, T, Ts...>
	{
		using Type = T;
	};

	template<usize I, typename T, typename... Ts>
	struct TTypeAt<I, T, Ts...> : TTypeAt<I - 1, Ts...> {};

	// 要素を作る印（値を渡す作り方を、コピー・移動と見分けるための引数）。
	struct FElementsTag
	{
		explicit FElementsTag() = default;
	};

	// I 番目の要素を一つ持つ入れ物。
	template<usize I, typename T>
	struct TLeaf
	{
		// 要素の値。
		T Value;

		// 値初期化する（引数なしで作れない型・参照は、この作り方が無い）。
		template<typename U = T, typename = TEnableIfT<bIsDefaultConstructible<U>>>
		constexpr TLeaf() : Value() {}

		// 渡された値から作る。TLeaf 自身（コピー・移動）は対象外。
		template<typename U, typename = TEnableIfT<!bIsSame<TDecayT<U>, TLeaf>>>
		constexpr explicit TLeaf( U&& InValue ) : Value( Forward<U>( InValue ) ) {}
	};

	// 添字 I... の入れ物を全部継承する。
	template<typename Indices, typename... Ts>
	struct TStorage;

	template<usize... I, typename... Ts>
	struct TStorage<TIndexSequence<I...>, Ts...> : TLeaf<I, Ts>...
	{
		constexpr TStorage() = default;

		template<typename... Us>
		constexpr explicit TStorage( FElementsTag, Us&&... InValues ) : TLeaf<I, Ts>( Forward<Us>( InValues ) )... {}
	};

	// Ts... の各要素を、Us... の各値から作れるか（個数が違えば作れない）。
	template<bool bSameCount, typename TsList, typename UsList>
	struct TAllConstructible : TFalseType {};

	template<typename... Ts, typename... Us>
	struct TAllConstructible<true, TTypeList<Ts...>, TTypeList<Us...>> : TIntegralConstant<bool, ( bIsConstructible<Ts, Us&&> && ... )> {};

	// 値が一つだけで、それが TTuple 自身か（コピー・移動と見分ける）。
	template<typename Self, typename... Us>
	inline constexpr bool bIsSelfOnly = false;

	template<typename Self, typename U>
	inline constexpr bool bIsSelfOnly<Self, U> = bIsSame<TDecayT<U>, Self>;
}

template<typename... Ts>
class TTuple : public NTupleDetail::TStorage<TMakeIndexSequence<sizeof...( Ts )>, Ts...>
{
	using FStorage = NTupleDetail::TStorage<TMakeIndexSequence<sizeof...( Ts )>, Ts...>;

	// Us... の値から作れるか（個数が同じで、各要素を各値から作れて、TTuple 自身のコピー・移動ではない）。
	template<typename... Us>
	static constexpr bool bCanConstructFrom = ( sizeof...( Us ) > 0 ) && NTupleDetail::TAllConstructible<sizeof...( Us ) == sizeof...( Ts ), NTupleDetail::TTypeList<Ts...>, NTupleDetail::TTypeList<Us...>>::Value && !NTupleDetail::bIsSelfOnly<TTuple, Us...>;

public:
	// 各要素を値初期化する。
	constexpr TTuple() = default;

	// 各要素を、渡された値から作る（個数は型の数と同じ。明示的に呼ぶ）。
	template<typename... Us, typename = TEnableIfT<bCanConstructFrom<Us...>>>
	constexpr explicit TTuple( Us&&... InValues ) : FStorage( NTupleDetail::FElementsTag(), Forward<Us>( InValues )... ) {}
};

//-----------------------------------------------------------------------------
// 個数と、I 番目の型
//-----------------------------------------------------------------------------
// 要素の数。
template<typename Tuple>
struct TTupleSize;

template<typename... Ts>
struct TTupleSize<TTuple<Ts...>> : TIntegralConstant<usize, sizeof...( Ts )> {};

template<typename Tuple>
struct TTupleSize<const Tuple> : TTupleSize<Tuple> {};

template<typename Tuple>
inline constexpr usize TupleSize = TTupleSize<Tuple>::Value;

// I 番目の要素の型。const の TTuple なら const を付ける。
template<usize I, typename Tuple>
struct TTupleElement;

template<usize I, typename... Ts>
struct TTupleElement<I, TTuple<Ts...>>
{
	static_assert( I < sizeof...( Ts ), "TTuple の要素の番号が範囲外" );
	using Type = typename NTupleDetail::TTypeAt<I, Ts...>::Type;
};

template<usize I, typename Tuple>
struct TTupleElement<I, const Tuple>
{
	using Type = TAddConstT<typename TTupleElement<I, Tuple>::Type>;
};

template<usize I, typename Tuple>
using TTupleElementT = typename TTupleElement<I, Tuple>::Type;

//-----------------------------------------------------------------------------
// 取り出す・作る・呼ぶ
//-----------------------------------------------------------------------------
namespace NTuple
{
	// I 番目の要素への参照。左辺値の TTuple からは左辺値参照、右辺値の TTuple からは右辺値参照が返る。
	template<usize I, typename... Ts>
	constexpr TTupleElementT<I, TTuple<Ts...>>& Get( TTuple<Ts...>& InTuple ) noexcept
	{
		return static_cast<NTupleDetail::TLeaf<I, TTupleElementT<I, TTuple<Ts...>>>&>( InTuple ).Value;
	}

	template<usize I, typename... Ts>
	constexpr const TTupleElementT<I, TTuple<Ts...>>& Get( const TTuple<Ts...>& InTuple ) noexcept
	{
		return static_cast<const NTupleDetail::TLeaf<I, TTupleElementT<I, TTuple<Ts...>>>&>( InTuple ).Value;
	}

	template<usize I, typename... Ts>
	constexpr TTupleElementT<I, TTuple<Ts...>>&& Get( TTuple<Ts...>&& InTuple ) noexcept
	{
		return static_cast<TTupleElementT<I, TTuple<Ts...>>&&>( Get<I>( InTuple ) );
	}

	// 値から TTuple を作る。各要素の型は TDecayT<値の型>（参照は外れ、配列はポインターになる）。
	template<typename... Ts>
	constexpr TTuple<TDecayT<Ts>...> Make( Ts&&... InValues )
	{
		return TTuple<TDecayT<Ts>...>( Forward<Ts>( InValues )... );
	}

}

namespace NTupleDetail
{
	// 添字 I... で全部の要素を取り出して、InFunction に渡す。
	template<typename Function, typename Tuple, usize... I>
	constexpr decltype( auto ) Apply( Function&& InFunction, Tuple&& InTuple, TIndexSequence<I...> )
	{
		return InFunction( NTuple::Get<I>( Forward<Tuple>( InTuple ) )... );
	}
}

namespace NTuple
{
	// 全部の要素を引数にして InFunction を呼び、その結果を返す。右辺値の TTuple を渡すと、要素も右辺値として渡る。
	template<typename Function, typename Tuple>
	constexpr decltype( auto ) Apply( Function&& InFunction, Tuple&& InTuple )
	{
		return NTupleDetail::Apply( Forward<Function>( InFunction ), Forward<Tuple>( InTuple ), TMakeIndexSequence<TupleSize<TRemoveReferenceT<Tuple>>>() );
	}
}
