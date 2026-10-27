// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NAlgo::LowerBound / UpperBound
//   整列済みの範囲 [First, Last) から、二分探索で位置を求める。std::lower_bound / std::upper_bound の代わり。
//   範囲は Pred( A, B )（A を B より前に置くなら true。省略すると operator<）の順に並んでいること。
//   First と Last は、添字で飛べる（+ と - ができる）反復子（ポインターなど）。
//
//   const float32* Found = NAlgo::UpperBound( Begin, End, Time );   // Time より大きい最初の要素の位置（無ければ End）
//=============================================================================
namespace NAlgo
{
	// Value 以上の最初の要素（Pred( 要素, Value ) が偽になる最初の要素）の位置。無ければ Last。
	template<typename Iterator, typename T, typename P>
	FORCEINLINE constexpr Iterator LowerBound( Iterator First, Iterator Last, const T& Value, P Pred )
	{
		isize Count = Last - First;
		while ( Count > 0 )
		{
			const isize Step = Count / 2;
			const Iterator Middle = First + Step;
			if ( Pred( *Middle, Value ) )
			{
				First = Middle + 1;
				Count -= Step + 1;
			}
			else
			{
				Count = Step;
			}
		}
		return First;
	}

	template<typename Iterator, typename T>
	FORCEINLINE constexpr Iterator LowerBound( Iterator First, Iterator Last, const T& Value ) { return LowerBound( First, Last, Value, TLess<>() ); }

	// Value より大きい最初の要素（Pred( Value, 要素 ) が真になる最初の要素）の位置。無ければ Last。
	template<typename Iterator, typename T, typename P>
	FORCEINLINE constexpr Iterator UpperBound( Iterator First, Iterator Last, const T& Value, P Pred )
	{
		isize Count = Last - First;
		while ( Count > 0 )
		{
			const isize Step = Count / 2;
			const Iterator Middle = First + Step;
			if ( !Pred( Value, *Middle ) )
			{
				First = Middle + 1;
				Count -= Step + 1;
			}
			else
			{
				Count = Step;
			}
		}
		return First;
	}

	template<typename Iterator, typename T>
	FORCEINLINE constexpr Iterator UpperBound( Iterator First, Iterator Last, const T& Value ) { return UpperBound( First, Last, Value, TLess<>() ); }
}
