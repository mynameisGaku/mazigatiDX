// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// 型の性質を調べる・型を作り替える（標準ライブラリに依存しない）
//   <type_traits> の代わり。結果は std と同じで、名前だけ違う。
//     std::is_same_v<A, B>            -> bIsSame<A, B>              （判定は bIs で始まる変数）
//     std::decay_t<T>                 -> TDecayT<T>                 （型を作り替えるものは T で始まり、末尾 T が結果の型）
//     std::enable_if_t<Cond, T>       -> TEnableIfT<Cond, T>
//     std::true_type / false_type     -> TTrueType / TFalseType     （値は Value）
//     std::index_sequence_for<Ts...>  -> TIndexSequenceFor<Ts...>
//   判定の多くはコンパイラの組み込み（__is_base_of など）を使う。
//=============================================================================

//-----------------------------------------------------------------------------
// 定数・選択
//-----------------------------------------------------------------------------
// 型 T の定数 InValue を持つ型。
template<typename T, T InValue>
struct TIntegralConstant
{
	using ValueType = T;
	using Type = TIntegralConstant;
	static constexpr T Value = InValue;
	constexpr operator ValueType() const noexcept { return Value; }
	constexpr ValueType operator()() const noexcept { return Value; }
};

// 真を表す型。判定の型を継承して使う（struct TIsX : TTrueType {}; として TIsX<T>::Value を調べる）。
using TTrueType = TIntegralConstant<bool, true>;
// 偽を表す型。
using TFalseType = TIntegralConstant<bool, false>;

// Condition が真のときだけ Type を持つ。テンプレートの候補を絞る（SFINAE）のに使う。
template<bool Condition, typename T = void>
struct TEnableIf {};

template<typename T>
struct TEnableIf<true, T>
{
	using Type = T;
};

template<bool Condition, typename T = void>
using TEnableIfT = typename TEnableIf<Condition, T>::Type;

// Condition が真なら TrueType、偽なら FalseType。
template<bool Condition, typename TrueType, typename FalseType>
struct TConditional
{
	using Type = TrueType;
};

template<typename TrueType, typename FalseType>
struct TConditional<false, TrueType, FalseType>
{
	using Type = FalseType;
};

template<bool Condition, typename TrueType, typename FalseType>
using TConditionalT = typename TConditional<Condition, TrueType, FalseType>::Type;

//-----------------------------------------------------------------------------
// 型の作り替え
//-----------------------------------------------------------------------------
// const を外す。
template<typename T> struct TRemoveConst { using Type = T; };
template<typename T> struct TRemoveConst<const T> { using Type = T; };
template<typename T> using TRemoveConstT = typename TRemoveConst<T>::Type;

// const を付ける（参照と関数にはもともと付かないので、そのまま）。
template<typename T> using TAddConstT = const T;

// volatile を外す。
template<typename T> struct TRemoveVolatile { using Type = T; };
template<typename T> struct TRemoveVolatile<volatile T> { using Type = T; };
template<typename T> using TRemoveVolatileT = typename TRemoveVolatile<T>::Type;

// const と volatile を外す。
template<typename T> struct TRemoveCV { using Type = TRemoveConstT<TRemoveVolatileT<T>>; };
template<typename T> using TRemoveCVT = typename TRemoveCV<T>::Type;

// ポインターを外す（const / volatile のポインターも）。
template<typename T> struct TRemovePointer { using Type = T; };
template<typename T> struct TRemovePointer<T*> { using Type = T; };
template<typename T> struct TRemovePointer<T* const> { using Type = T; };
template<typename T> struct TRemovePointer<T* volatile> { using Type = T; };
template<typename T> struct TRemovePointer<T* const volatile> { using Type = T; };
template<typename T> using TRemovePointerT = typename TRemovePointer<T>::Type;

// 配列の一番外側の次元を外す。
template<typename T> struct TRemoveExtent { using Type = T; };
template<typename T> struct TRemoveExtent<T[]> { using Type = T; };
template<typename T, usize N> struct TRemoveExtent<T[N]> { using Type = T; };
template<typename T> using TRemoveExtentT = typename TRemoveExtent<T>::Type;

// 参照を外した型へのポインター。
template<typename T> struct TAddPointer { using Type = TRemoveReferenceT<T>*; };
template<typename T> using TAddPointerT = typename TAddPointer<T>::Type;

// 列挙型の中身の整数型。
template<typename T>
using TUnderlyingTypeT = __underlying_type( T );

//-----------------------------------------------------------------------------
// 参照を付ける（void など、参照を作れない型はそのまま）
//-----------------------------------------------------------------------------
namespace NTypeTraitsPrivate
{
	// 型を並べて void にする（テンプレートの特殊化で、その型が書けるかを調べるための道具）。
	template<typename...> struct TMakeVoid { using Type = void; };
	template<typename... Types> using TVoidT = typename TMakeVoid<Types...>::Type;

	template<typename T, typename = void> struct TAddLvalueReferenceImpl { using Type = T; };
	template<typename T> struct TAddLvalueReferenceImpl<T, TVoidT<T&>> { using Type = T&; };

	template<typename T, typename = void> struct TAddRvalueReferenceImpl { using Type = T; };
	template<typename T> struct TAddRvalueReferenceImpl<T, TVoidT<T&&>> { using Type = T&&; };
}

// 型を並べて void にする。テンプレートの特殊化で「その式や型が書けるか」を調べる（書けないと特殊化が選ばれない）のに使う。
template<typename... Types> using TVoidT = NTypeTraitsPrivate::TVoidT<Types...>;

// T の左辺値参照。参照を作れない型（void）はそのまま。
template<typename T> using TAddLvalueReferenceT = typename NTypeTraitsPrivate::TAddLvalueReferenceImpl<T>::Type;
// T の右辺値参照。参照を作れない型（void）はそのまま。
template<typename T> using TAddRvalueReferenceT = typename NTypeTraitsPrivate::TAddRvalueReferenceImpl<T>::Type;

//-----------------------------------------------------------------------------
// 判定
//-----------------------------------------------------------------------------
// 二つの型が同じか。
template<typename A, typename B> inline constexpr bool bIsSame = false;
template<typename A> inline constexpr bool bIsSame<A, A> = true;

// T のいずれかと同じか（内部用）。
template<typename T, typename... Ts> inline constexpr bool bIsOneOf = ( bIsSame<T, Ts> || ... );

// const か。
template<typename T> inline constexpr bool bIsConst = false;
template<typename T> inline constexpr bool bIsConst<const T> = true;

// 左辺値参照か。
template<typename T> inline constexpr bool bIsLvalueReference = false;
template<typename T> inline constexpr bool bIsLvalueReference<T&> = true;

// 右辺値参照か。
template<typename T> inline constexpr bool bIsRvalueReference = false;
template<typename T> inline constexpr bool bIsRvalueReference<T&&> = true;

// 参照か。
template<typename T> inline constexpr bool bIsReference = bIsLvalueReference<T> || bIsRvalueReference<T>;

// 配列か（大きさ不明の配列も）。
template<typename T> inline constexpr bool bIsArray = false;
template<typename T> inline constexpr bool bIsArray<T[]> = true;
template<typename T, usize N> inline constexpr bool bIsArray<T[N]> = true;

// 配列の一番外側の次元の大きさ。配列でない・大きさ不明なら 0。
template<typename T> inline constexpr usize ArrayExtent = 0;
template<typename T, usize N> inline constexpr usize ArrayExtent<T[N]> = N;

// ポインターか（const / volatile のポインターも）。メンバーへのポインターは含まない。
namespace NTypeTraitsPrivate
{
	template<typename T> inline constexpr bool bIsPointerImpl = false;
	template<typename T> inline constexpr bool bIsPointerImpl<T*> = true;
}
template<typename T> inline constexpr bool bIsPointer = NTypeTraitsPrivate::bIsPointerImpl<TRemoveCVT<T>>;

// void か（const / volatile の void も）。
template<typename T> inline constexpr bool bIsVoid = bIsSame<TRemoveCVT<T>, void>;

// 整数型か（bool と文字型を含む）。
template<typename T> inline constexpr bool bIsIntegral = bIsOneOf<TRemoveCVT<T>, bool, char, signed char, unsigned char, wchar_t, char16_t, char32_t, short, unsigned short, int, unsigned int, long, unsigned long, long long, unsigned long long>;

// 浮動小数点型か。
template<typename T> inline constexpr bool bIsFloatingPoint = bIsOneOf<TRemoveCVT<T>, float, double, long double>;

// 整数型か浮動小数点型か。
template<typename T> inline constexpr bool bIsArithmetic = bIsIntegral<T> || bIsFloatingPoint<T>;

// 列挙型か（enum class も）。
template<typename T> inline constexpr bool bIsEnum = __is_enum( T );

// 関数の型か。const を付けても変わらず、参照でもない型は、関数の型だけ。
#pragma warning( push )
#pragma warning( disable : 4180 )   // 関数の型に const を付けても意味がない（ここではそれを利用して判定する）
template<typename T> inline constexpr bool bIsFunction = !bIsConst<const T> && !bIsReference<T>;
#pragma warning( pop )

// 純粋仮想関数を持つクラスか。
template<typename T> inline constexpr bool bIsAbstract = __is_abstract( T );

// Derived が Base を継承している（または同じクラス）か。
template<typename Base, typename Derived> inline constexpr bool bIsBaseOf = __is_base_of( Base, Derived );

// From から To へ暗黙に変換できるか。
template<typename From, typename To> inline constexpr bool bIsConvertible = __is_convertible_to( From, To );

// バイト単位でコピーしても壊れないか。
template<typename T> inline constexpr bool bIsTriviallyCopyable = __is_trivially_copyable( T );

// 壊すときに何もしなくてよいか。
template<typename T> inline constexpr bool bIsTriviallyDestructible = __is_trivially_destructible( T );

// 引数なしで作れるか。
template<typename T> inline constexpr bool bIsDefaultConstructible = __is_constructible( T );

// Args... を引数にして作れるか。
template<typename T, typename... Args> inline constexpr bool bIsConstructible = __is_constructible( T, Args... );

// 例外を投げずに移動して作れるか。
template<typename T> inline constexpr bool bIsNothrowMoveConstructible = __is_nothrow_constructible( T, TAddRvalueReferenceT<T> );

// 例外を投げずに移動して代入できるか。
template<typename T> inline constexpr bool bIsNothrowMoveAssignable = __is_nothrow_assignable( TAddLvalueReferenceT<T>, TAddRvalueReferenceT<T> );

//-----------------------------------------------------------------------------
// decay（値として渡したときの型）
//-----------------------------------------------------------------------------
// 参照を外し、配列と関数はポインターにし、それ以外は const / volatile を外す。値渡しの型を決めるのに使う。
template<typename T>
struct TDecay
{
private:
	using Bare = TRemoveReferenceT<T>;

public:
	using Type = TConditionalT<bIsArray<Bare>, TAddPointerT<TRemoveExtentT<Bare>>, TConditionalT<bIsFunction<Bare>, TAddPointerT<Bare>, TRemoveCVT<Bare>>>;
};

template<typename T>
using TDecayT = typename TDecay<T>::Type;

//-----------------------------------------------------------------------------
// 式の型を調べる
//-----------------------------------------------------------------------------
// 値を作らずに「T の右辺値」を式の中で使うための宣言（decltype の中だけで使う。定義は無い）。
template<typename T>
TAddRvalueReferenceT<T> DeclVal() noexcept;

// Function を Args... で呼んだ結果の型。
template<typename Function, typename... Args>
using TInvokeResultT = decltype( DeclVal<Function>()( DeclVal<Args>()... ) );

//-----------------------------------------------------------------------------
// 整数の並び（パラメーターパックを添字で展開するのに使う）
//   TIndexSequence<0, 1, 2>、TMakeIndexSequence<3>（0 .. 2）、TIndexSequenceFor<A, B, C>（型の数だけ）
//-----------------------------------------------------------------------------
template<usize... Indices>
struct TIndexSequence
{
	// 並びの長さ。
	static constexpr usize Size() noexcept { return sizeof...( Indices ); }
};

namespace NTypeTraitsPrivate
{
	template<usize N, usize... Indices>
	struct TMakeIndexSequenceImpl : TMakeIndexSequenceImpl<N - 1, N - 1, Indices...> {};

	template<usize... Indices>
	struct TMakeIndexSequenceImpl<0, Indices...>
	{
		using Type = TIndexSequence<Indices...>;
	};
}

template<usize N>
using TMakeIndexSequence = typename NTypeTraitsPrivate::TMakeIndexSequenceImpl<N>::Type;

template<typename... Types>
using TIndexSequenceFor = TMakeIndexSequence<sizeof...( Types )>;
