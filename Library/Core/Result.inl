#pragma once

#include "Library/Core/Types/Utility.h"

// Result.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Result.h の末尾から include される。直接 include しない

template<typename T, typename E>
template<typename Self, typename F>
auto TResult<T, E>::MapImpl( Self&& This, F&& Func )
{
	using U   = std::decay_t<std::invoke_result_t<F, decltype( ForwardOk( Forward<Self>( This ) ) )>>;
	using Out = TResult<U, E>;
	if ( This.IsErr() ) return Out::Err( ForwardErr( Forward<Self>( This ) ) );
	if constexpr ( std::is_void_v<U> )
	{
		Forward<F>( Func )( ForwardOk( Forward<Self>( This ) ) );
		return Out::Ok();
	}
	else
	{
		return Out::Ok( Forward<F>( Func )( ForwardOk( Forward<Self>( This ) ) ) );
	}
}

template<typename E>
template<typename F>
auto TResult<void, E>::Map( F&& Func ) const
{
	using U   = std::decay_t<std::invoke_result_t<F>>;
	using Out = TResult<U, E>;
	if ( IsErr() ) return Out::Err( Error.GetValue() );
	if constexpr ( std::is_void_v<U> )
	{
		Forward<F>( Func )();
		return Out::Ok();
	}
	else
	{
		return Out::Ok( Forward<F>( Func )() );
	}
}