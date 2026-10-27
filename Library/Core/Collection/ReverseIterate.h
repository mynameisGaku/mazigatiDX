// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_COLLECTION_REVERSE_ITERATE_H
#define FRAMEWORK_CORE_COLLECTION_REVERSE_ITERATE_H

#include "Library/Core/Collection/RangeAccess.h"
#include "Library/Core/Types/CompilerMacros.h"

// 後退可能な反復子を持つ配列などを逆順に参照する。元の入れ物を所有しない。
template<typename ContainerType>
class TReverseRange
{
public:
	// 入れ物の寿命はこの範囲より長く保つ。
	FORCEINLINE explicit TReverseRange( ContainerType& InContainer ) : Container( InContainer ) {}
	// 元の終端の直前から走査する。
	FORCEINLINE auto begin() const { return NRange::MakeReverse( NRange::End( Container ) ); }
	// 元の先頭の直前を終端にする。
	FORCEINLINE auto end() const { return NRange::MakeReverse( NRange::Begin( Container ) ); }

private:
	// 逆順で参照する入れ物。
	ContainerType& Container;
};

// 配列などの逆順ループを作る。一時的な入れ物は受け付けない。
template<typename ContainerType>
FORCEINLINE TReverseRange<ContainerType> ReverseIterate( ContainerType& Container ) { return TReverseRange<ContainerType>( Container ); }

#endif
