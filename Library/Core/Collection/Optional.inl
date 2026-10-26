#pragma once

#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

// Optional.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Optional.h の末尾から include される。直接 include しない

template<typename T>
auto TOptional<T>::operator=( const TOptional& Other ) -> TOptional&
{
	if ( this == &Other ) return *this;
	if ( Other.bIsSet )
	{
		if ( bIsSet ) Ref() = Other.Ref();
		else Emplace( Other.Ref() );
	}
	else
	{
		Reset();
	}
	return *this;
}

template<typename T>
auto TOptional<T>::operator=( TOptional&& Other ) noexcept( bIsNothrowMoveAssignable<T> ) -> TOptional&
{
	if ( this == &Other ) return *this;
	if ( Other.bIsSet )
	{
		if ( bIsSet ) Ref() = MoveTemp( Other.Ref() );
		else Emplace( MoveTemp( Other.Ref() ) );
	}
	else
	{
		Reset();
	}
	return *this;
}