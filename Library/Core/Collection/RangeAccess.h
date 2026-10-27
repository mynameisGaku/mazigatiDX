// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NRange / TReverseIterator
//   入れ物（begin() / end() を持つもの）や配列の、先頭と終端の反復子を得る。
//   std::begin / std::end / std::make_reverse_iterator の代わり。標準ライブラリに依存しない。
//
//   auto First = NRange::Begin( Container );        // Container.begin()（配列なら先頭のポインター）
//   auto Last = NRange::End( Container );
//   TReverseIterator<int32*> It = NRange::MakeReverse( Last );   // 逆向きに走査する反復子
//=============================================================================
namespace NRange
{
	// 入れ物の先頭の反復子。
	template<typename Container>
	FORCEINLINE constexpr auto Begin( Container& InContainer ) -> decltype( InContainer.begin() ) { return InContainer.begin(); }

	template<typename Container>
	FORCEINLINE constexpr auto Begin( const Container& InContainer ) -> decltype( InContainer.begin() ) { return InContainer.begin(); }

	// 配列の先頭のポインター。
	template<typename T, usize N>
	FORCEINLINE constexpr T* Begin( T ( &InArray )[N] ) noexcept { return InArray; }

	// 入れ物の終端の反復子。
	template<typename Container>
	FORCEINLINE constexpr auto End( Container& InContainer ) -> decltype( InContainer.end() ) { return InContainer.end(); }

	template<typename Container>
	FORCEINLINE constexpr auto End( const Container& InContainer ) -> decltype( InContainer.end() ) { return InContainer.end(); }

	// 配列の終端（最後の要素の次）のポインター。
	template<typename T, usize N>
	FORCEINLINE constexpr T* End( T ( &InArray )[N] ) noexcept { return InArray + N; }
}

//-----------------------------------------------------------------------------
// TReverseIterator<Iterator>
//   Iterator を逆向きに進める反復子。元の位置は「次に読む要素の一つ後ろ」を指す（std::reverse_iterator と同じ）。
//   元の反復子は、前へ戻れる（--）ものに限る。
//-----------------------------------------------------------------------------
template<typename Iterator>
class TReverseIterator
{
public:
	// 元の位置を持たない状態。
	constexpr TReverseIterator() = default;
	// InBase の一つ手前の要素から、逆向きに走査する。
	FORCEINLINE constexpr explicit TReverseIterator( Iterator InBase ) : Base( InBase ) {}

	// 元の位置の一つ手前の要素。
	FORCEINLINE constexpr decltype( auto ) operator*() const
	{
		Iterator Previous = Base;
		--Previous;
		return *Previous;
	}

	// 一つ進める（元の位置は一つ戻る）。
	FORCEINLINE constexpr TReverseIterator& operator++()
	{
		--Base;
		return *this;
	}

	FORCEINLINE constexpr TReverseIterator operator++( int )
	{
		TReverseIterator Copy = *this;
		--Base;
		return Copy;
	}

	// 元の位置。
	FORCEINLINE constexpr Iterator GetBase() const { return Base; }

	FORCEINLINE constexpr bool operator==( const TReverseIterator& Other ) const { return Base == Other.Base; }
	FORCEINLINE constexpr bool operator!=( const TReverseIterator& Other ) const { return !( Base == Other.Base ); }

private:
	// 元の位置。
	Iterator Base {};
};

namespace NRange
{
	// Base の一つ手前から逆向きに走査する反復子を作る。
	template<typename Iterator>
	FORCEINLINE constexpr TReverseIterator<Iterator> MakeReverse( Iterator Base ) { return TReverseIterator<Iterator>( Base ); }
}
