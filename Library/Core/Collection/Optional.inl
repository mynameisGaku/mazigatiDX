#pragma once

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
auto TOptional<T>::operator=( TOptional&& Other ) noexcept( std::is_nothrow_move_assignable_v<T> ) -> TOptional&
{
	if ( this == &Other ) return *this;
	if ( Other.bIsSet )
	{
		if ( bIsSet ) Ref() = std::move( Other.Ref() );
		else Emplace( std::move( Other.Ref() ) );
	}
	else
	{
		Reset();
	}
	return *this;
}