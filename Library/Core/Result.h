#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// TResult<T, E>
//   std::expected (C++23) 相当。成功値 T か エラー値 E のどちらか一方を保持する。
//   値は内部のバッファに直接置かれる（ヒープ確保なし）。
//
//   auto r = TResult<int32>::Ok( 42 );
//   auto e = TResult<int32>::Err( "failed" );       // エラー型の既定は FString
//   if ( r.IsOk() ) r.Unwrap();
//   r.Map( []( int32 v ){ return v * 2; } );
//   r.AndThen( []( int32 v ){ return TResult<float32>::Ok( v ); } );
//
//   TResult<void> は値を持たない成功/失敗を表す。
//=============================================================================

template<typename T, typename E = FString>
class TResult;

namespace NResultDetail
{
	template<typename R>
	struct TIsResult : std::false_type {};

	template<typename T, typename E>
	struct TIsResult<TResult<T, E>> : std::true_type {};

	[[noreturn]] FORCENOINLINE inline void Panic( const char* Message )
	{
		assert( false && Message );
		(void)Message;
		std::abort();
	}

	struct FOkTag {};
	struct FErrTag {};
}

//=============================================================================
// 値あり
//=============================================================================
template<typename T, typename E>
class TResult
{
	static_assert( !std::is_reference_v<T>, "TResult<T&> is not supported" );
	static_assert( !std::is_reference_v<E>, "TResult<T, E&> is not supported" );

	template<typename, typename>
	friend class TResult;

public:
	using ValueType = T;
	using ErrorType = E;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	template<typename... Args>
	FORCEINLINE static TResult Ok( Args&&... args ) { return TResult( NResultDetail::FOkTag(), std::forward<Args>( args )... ); }

	template<typename... Args>
	FORCEINLINE static TResult Err( Args&&... args ) { return TResult( NResultDetail::FErrTag(), std::forward<Args>( args )... ); }

	FORCEINLINE TResult( const TResult& Other ) : bIsOk( Other.bIsOk )
	{
		if ( bIsOk ) new ( Storage ) T( Other.OkRef() );
		else new ( Storage ) E( Other.ErrRef() );
	}

	TResult( TResult&& Other ) noexcept( std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> )
		: bIsOk( Other.bIsOk )
	{
		if ( bIsOk ) new ( Storage ) T( std::move( Other.OkRef() ) );
		else new ( Storage ) E( std::move( Other.ErrRef() ) );
	}

	FORCEINLINE ~TResult() { Destroy(); }

	FORCEINLINE TResult& operator=( const TResult& Other )
	{
		if ( this != &Other )
		{
			Destroy();
			bIsOk = Other.bIsOk;
			if ( bIsOk ) new ( Storage ) T( Other.OkRef() );
			else new ( Storage ) E( Other.ErrRef() );
		}
		return *this;
	}

	FORCEINLINE TResult& operator=( TResult&& Other ) noexcept( std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> )
	{
		if ( this != &Other )
		{
			Destroy();
			bIsOk = Other.bIsOk;
			if ( bIsOk ) new ( Storage ) T( std::move( Other.OkRef() ) );
			else new ( Storage ) E( std::move( Other.ErrRef() ) );
		}
		return *this;
	}

	//-------------------------------------------------------------------------
	// 状態
	//-------------------------------------------------------------------------
	FORCEINLINE bool IsOk() const noexcept { return bIsOk; }
	FORCEINLINE bool IsErr() const noexcept { return !bIsOk; }
	FORCEINLINE explicit operator bool() const noexcept { return bIsOk; }

	//-------------------------------------------------------------------------
	// 値の取り出し
	//-------------------------------------------------------------------------
	// エラー時に呼ぶと abort する
	FORCEINLINE T& Unwrap() &
	{
		if ( !bIsOk ) NResultDetail::Panic( "TResult::Unwrap() called on Err" );
		return OkRef();
	}
	FORCEINLINE const T& Unwrap() const&
	{
		if ( !bIsOk ) NResultDetail::Panic( "TResult::Unwrap() called on Err" );
		return OkRef();
	}
	FORCEINLINE T&& Unwrap() &&
	{
		if ( !bIsOk ) NResultDetail::Panic( "TResult::Unwrap() called on Err" );
		return std::move( OkRef() );
	}

	// 成功時に呼ぶと abort する
	FORCEINLINE E& UnwrapErr() &
	{
		if ( bIsOk ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return ErrRef();
	}
	FORCEINLINE const E& UnwrapErr() const&
	{
		if ( bIsOk ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return ErrRef();
	}
	FORCEINLINE E&& UnwrapErr() &&
	{
		if ( bIsOk ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return std::move( ErrRef() );
	}

	template<typename U>
	FORCEINLINE T UnwrapOr( U&& Default ) const& { return bIsOk ? OkRef() : static_cast<T>( std::forward<U>( Default ) ); }
	template<typename U>
	FORCEINLINE T UnwrapOr( U&& Default ) && { return bIsOk ? std::move( OkRef() ) : static_cast<T>( std::forward<U>( Default ) ); }

	template<typename F>
	FORCEINLINE T UnwrapOrElse( F&& Func ) const& { return bIsOk ? OkRef() : static_cast<T>( std::forward<F>( Func )( ErrRef() ) ); }

	// 値へのポインタ（エラー時は nullptr）
	FORCEINLINE T* TryGet() noexcept { return bIsOk ? &OkRef() : nullptr; }
	FORCEINLINE const T* TryGet() const noexcept { return bIsOk ? &OkRef() : nullptr; }

	// エラーへのポインタ（成功時は nullptr）
	FORCEINLINE E* TryGetErr() noexcept { return bIsOk ? nullptr : &ErrRef(); }
	FORCEINLINE const E* TryGetErr() const noexcept { return bIsOk ? nullptr : &ErrRef(); }

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	// Ok(T) -> Ok(F(T))、Err はそのまま
	template<typename F>
	FORCEINLINE auto Map( F&& Func ) const& { return MapImpl( *this, std::forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto Map( F&& Func ) && { return MapImpl( std::move( *this ), std::forward<F>( Func ) ); }

	// Err(E) -> Err(F(E))、Ok はそのまま
	template<typename F>
	FORCEINLINE auto MapErr( F&& Func ) const& { return MapErrImpl( *this, std::forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto MapErr( F&& Func ) && { return MapErrImpl( std::move( *this ), std::forward<F>( Func ) ); }

	// Ok(T) -> F(T) (F は TResult<U, E> を返す)、Err はそのまま
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) const& { return AndThenImpl( *this, std::forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) && { return AndThenImpl( std::move( *this ), std::forward<F>( Func ) ); }

	// Err(E) -> F(E) (F は TResult<T, E2> を返す)、Ok はそのまま
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) const& { return OrElseImpl( *this, std::forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) && { return OrElseImpl( std::move( *this ), std::forward<F>( Func ) ); }

private:
	template<typename... Args>
	FORCEINLINE explicit TResult( NResultDetail::FOkTag, Args&&... args ) : bIsOk( true )
	{
		new ( Storage ) T( std::forward<Args>( args )... );
	}

	template<typename... Args>
	FORCEINLINE explicit TResult( NResultDetail::FErrTag, Args&&... args ) : bIsOk( false )
	{
		new ( Storage ) E( std::forward<Args>( args )... );
	}

	FORCEINLINE void Destroy() noexcept
	{
		if ( bIsOk ) OkRef().~T();
		else ErrRef().~E();
	}

	FORCEINLINE T& OkRef() noexcept { return *std::launder( reinterpret_cast<T*>( Storage ) ); }
	FORCEINLINE const T& OkRef() const noexcept { return *std::launder( reinterpret_cast<const T*>( Storage ) ); }
	FORCEINLINE E& ErrRef() noexcept { return *std::launder( reinterpret_cast<E*>( Storage ) ); }
	FORCEINLINE const E& ErrRef() const noexcept { return *std::launder( reinterpret_cast<const E*>( Storage ) ); }

	// Self の値カテゴリ（lvalue / rvalue）を保ったまま成功値・エラー値を取り出す
	template<typename Self>
	FORCEINLINE static decltype( auto ) ForwardOk( Self&& This )
	{
		if constexpr ( std::is_lvalue_reference_v<Self> ) return ( This.OkRef() );
		else return std::move( This.OkRef() );
	}
	template<typename Self>
	FORCEINLINE static decltype( auto ) ForwardErr( Self&& This )
	{
		if constexpr ( std::is_lvalue_reference_v<Self> ) return ( This.ErrRef() );
		else return std::move( This.ErrRef() );
	}

	template<typename Self, typename F>
	static auto MapImpl( Self&& This, F&& Func );

	template<typename Self, typename F>
	FORCEINLINE static auto MapErrImpl( Self&& This, F&& Func )
	{
		using E2  = std::decay_t<std::invoke_result_t<F, decltype( ForwardErr( std::forward<Self>( This ) ) )>>;
		using Out = TResult<T, E2>;
		if ( This.IsOk() ) return Out::Ok( ForwardOk( std::forward<Self>( This ) ) );
		return Out::Err( std::forward<F>( Func )( ForwardErr( std::forward<Self>( This ) ) ) );
	}

	template<typename Self, typename F>
	FORCEINLINE static auto AndThenImpl( Self&& This, F&& Func )
	{
		using Out = std::decay_t<std::invoke_result_t<F, decltype( ForwardOk( std::forward<Self>( This ) ) )>>;
		static_assert( NResultDetail::TIsResult<Out>::value, "AndThen: callback must return TResult" );
		static_assert( std::is_same_v<typename Out::ErrorType, E>, "AndThen: callback must return the same error type" );
		if ( This.IsErr() ) return Out::Err( ForwardErr( std::forward<Self>( This ) ) );
		return std::forward<F>( Func )( ForwardOk( std::forward<Self>( This ) ) );
	}

	template<typename Self, typename F>
	FORCEINLINE static auto OrElseImpl( Self&& This, F&& Func )
	{
		using Out = std::decay_t<std::invoke_result_t<F, decltype( ForwardErr( std::forward<Self>( This ) ) )>>;
		static_assert( NResultDetail::TIsResult<Out>::value, "OrElse: callback must return TResult" );
		static_assert( std::is_same_v<typename Out::ValueType, T>, "OrElse: callback must return the same value type" );
		if ( This.IsOk() ) return Out::Ok( ForwardOk( std::forward<Self>( This ) ) );
		return std::forward<F>( Func )( ForwardErr( std::forward<Self>( This ) ) );
	}

	alignas( T ) alignas( E ) unsigned char Storage[sizeof( T ) > sizeof( E ) ? sizeof( T ) : sizeof( E )];
	bool bIsOk;
};

//=============================================================================
// 値なし (TResult<void, E>)
//=============================================================================
template<typename E>
class TResult<void, E>
{
	static_assert( !std::is_reference_v<E>, "TResult<void, E&> is not supported" );

public:
	using ValueType = void;
	using ErrorType = E;

	FORCEINLINE static TResult Ok() { return TResult(); }

	template<typename... Args>
	FORCEINLINE static TResult Err( Args&&... args )
	{
		TResult Result;
		Result.Error.Emplace( std::forward<Args>( args )... );
		return Result;
	}

	FORCEINLINE bool IsOk() const noexcept { return !Error.IsSet(); }
	FORCEINLINE bool IsErr() const noexcept { return Error.IsSet(); }
	FORCEINLINE explicit operator bool() const noexcept { return IsOk(); }

	// エラー時に呼ぶと abort する
	FORCEINLINE void Unwrap() const
	{
		if ( IsErr() ) NResultDetail::Panic( "TResult::Unwrap() called on Err" );
	}

	// 成功時に呼ぶと abort する
	FORCEINLINE E& UnwrapErr() &
	{
		if ( IsOk() ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return Error.GetValue();
	}
	FORCEINLINE const E& UnwrapErr() const&
	{
		if ( IsOk() ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return Error.GetValue();
	}
	FORCEINLINE E&& UnwrapErr() &&
	{
		if ( IsOk() ) NResultDetail::Panic( "TResult::UnwrapErr() called on Ok" );
		return std::move( Error ).GetValue();
	}

	FORCEINLINE E* TryGetErr() noexcept { return Error.GetPtrOrNull(); }
	FORCEINLINE const E* TryGetErr() const noexcept { return Error.GetPtrOrNull(); }

	// Ok() -> Ok(F())、Err はそのまま
	template<typename F>
	auto Map( F&& Func ) const;

	// Err(E) -> Err(F(E))、Ok はそのまま
	template<typename F>
	FORCEINLINE auto MapErr( F&& Func ) const
	{
		using E2  = std::decay_t<std::invoke_result_t<F, const E&>>;
		using Out = TResult<void, E2>;
		if ( IsOk() ) return Out::Ok();
		return Out::Err( std::forward<F>( Func )( Error.GetValue() ) );
	}

	// Ok() -> F() (F は TResult<U, E> を返す)、Err はそのまま
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) const
	{
		using Out = std::decay_t<std::invoke_result_t<F>>;
		static_assert( NResultDetail::TIsResult<Out>::value, "AndThen: callback must return TResult" );
		static_assert( std::is_same_v<typename Out::ErrorType, E>, "AndThen: callback must return the same error type" );
		if ( IsErr() ) return Out::Err( Error.GetValue() );
		return std::forward<F>( Func )();
	}

	// Err(E) -> F(E) (F は TResult<void, E2> を返す)、Ok はそのまま
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) const
	{
		using Out = std::decay_t<std::invoke_result_t<F, const E&>>;
		static_assert( NResultDetail::TIsResult<Out>::value, "OrElse: callback must return TResult" );
		static_assert( std::is_void_v<typename Out::ValueType>, "OrElse: callback must return TResult<void, ...>" );
		if ( IsOk() ) return Out::Ok();
		return std::forward<F>( Func )( Error.GetValue() );
	}

private:
	TResult() = default;

	TOptional<E> Error;
};

#include "Result.inl"
