// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/CompilerMacros.h"

//=============================================================================
// 型の補助（右辺値参照の受け渡し）
//   <utility> の std::move / std::forward の代わり。標準ライブラリに依存しない。
//
//   Name = MoveTemp( Other );                            // Other の中身を移す（Other は使い終わったものとして扱う）
//   template<typename T> void Add( T&& Value ) { Items.Add( Forward<T>( Value ) ); }   // 左辺値・右辺値をそのまま渡す
//=============================================================================
// 参照を外した型。T が T& / T&& のとき T、それ以外は T のまま。
template<typename T>
struct TRemoveReference
{
	// 参照を外した結果の型。
	using Type = T;
};

// 左辺値参照を外す特殊化。
template<typename T>
struct TRemoveReference<T&>
{
	// 参照を外した結果の型。
	using Type = T;
};

// 右辺値参照を外す特殊化。
template<typename T>
struct TRemoveReference<T&&>
{
	// 参照を外した結果の型。
	using Type = T;
};

// TRemoveReference<T>::Type の短い書き方。
template<typename T>
using TRemoveReferenceT = typename TRemoveReference<T>::Type;

// 引数を右辺値として返す。中身を移してよい相手に渡すときに使う（コピーを避ける）。
template<typename T>
FORCEINLINE constexpr TRemoveReferenceT<T>&& MoveTemp( T&& Object ) noexcept
{
	return static_cast<TRemoveReferenceT<T>&&>( Object );
}

// テンプレート引数 T の値の種類（左辺値 / 右辺値）を保って、そのまま渡す。左辺値を受けたときの版。
template<typename T>
FORCEINLINE constexpr T&& Forward( TRemoveReferenceT<T>& Object ) noexcept
{
	return static_cast<T&&>( Object );
}

// テンプレート引数 T の値の種類（左辺値 / 右辺値）を保って、そのまま渡す。右辺値を受けたときの版。
template<typename T>
FORCEINLINE constexpr T&& Forward( TRemoveReferenceT<T>&& Object ) noexcept
{
	return static_cast<T&&>( Object );
}
