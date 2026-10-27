#pragma once
#include <cassert>
#include <new>

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/Process.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

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
	struct TIsResult : TFalseType {};

	template<typename T, typename E>
	struct TIsResult<TResult<T, E>> : TTrueType {};

	[[noreturn]] FORCENOINLINE inline void Panic( const char* Message )
	{
		assert( false && Message );
		(void)Message;
		NProcess::Abort();
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
	static_assert( !bIsReference<T>, "TResult<T&> is not supported" );
	static_assert( !bIsReference<E>, "TResult<T, E&> is not supported" );

	template<typename, typename>
	friend class TResult;

public:
	using ValueType = T;
	using ErrorType = E;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	template<typename... Args>
	FORCEINLINE static TResult Ok( Args&&... args ) { return TResult( NResultDetail::FOkTag(), Forward<Args>( args )... ); }

	template<typename... Args>
	FORCEINLINE static TResult Err( Args&&... args ) { return TResult( NResultDetail::FErrTag(), Forward<Args>( args )... ); }

	FORCEINLINE TResult( const TResult& Other ) : bIsOk( Other.bIsOk )
	{
		if ( bIsOk ) new ( Storage ) T( Other.OkRef() );
		else new ( Storage ) E( Other.ErrRef() );
	}

	TResult( TResult&& Other ) noexcept( bIsNothrowMoveConstructible<T> && bIsNothrowMoveConstructible<E> )
		: bIsOk( Other.bIsOk )
	{
		if ( bIsOk ) new ( Storage ) T( MoveTemp( Other.OkRef() ) );
		else new ( Storage ) E( MoveTemp( Other.ErrRef() ) );
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

	FORCEINLINE TResult& operator=( TResult&& Other ) noexcept( bIsNothrowMoveConstructible<T> && bIsNothrowMoveConstructible<E> )
	{
		if ( this != &Other )
		{
			Destroy();
			bIsOk = Other.bIsOk;
			if ( bIsOk ) new ( Storage ) T( MoveTemp( Other.OkRef() ) );
			else new ( Storage ) E( MoveTemp( Other.ErrRef() ) );
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
		return MoveTemp( OkRef() );
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
		return MoveTemp( ErrRef() );
	}

	template<typename U>
	FORCEINLINE T UnwrapOr( U&& Default ) const& { return bIsOk ? OkRef() : static_cast<T>( Forward<U>( Default ) ); }
	template<typename U>
	FORCEINLINE T UnwrapOr( U&& Default ) && { return bIsOk ? MoveTemp( OkRef() ) : static_cast<T>( Forward<U>( Default ) ); }

	template<typename F>
	FORCEINLINE T UnwrapOrElse( F&& Func ) const& { return bIsOk ? OkRef() : static_cast<T>( Forward<F>( Func )( ErrRef() ) ); }

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
	FORCEINLINE auto Map( F&& Func ) const& { return MapImpl( *this, Forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto Map( F&& Func ) && { return MapImpl( MoveTemp( *this ), Forward<F>( Func ) ); }

	// Err(E) -> Err(F(E))、Ok はそのまま
	template<typename F>
	FORCEINLINE auto MapErr( F&& Func ) const& { return MapErrImpl( *this, Forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto MapErr( F&& Func ) && { return MapErrImpl( MoveTemp( *this ), Forward<F>( Func ) ); }

	// Ok(T) -> F(T) (F は TResult<U, E> を返す)、Err はそのまま
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) const& { return AndThenImpl( *this, Forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) && { return AndThenImpl( MoveTemp( *this ), Forward<F>( Func ) ); }

	// Err(E) -> F(E) (F は TResult<T, E2> を返す)、Ok はそのまま
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) const& { return OrElseImpl( *this, Forward<F>( Func ) ); }
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) && { return OrElseImpl( MoveTemp( *this ), Forward<F>( Func ) ); }

private:
	template<typename... Args>
	FORCEINLINE explicit TResult( NResultDetail::FOkTag, Args&&... args ) : bIsOk( true )
	{
		new ( Storage ) T( Forward<Args>( args )... );
	}

	template<typename... Args>
	FORCEINLINE explicit TResult( NResultDetail::FErrTag, Args&&... args ) : bIsOk( false )
	{
		new ( Storage ) E( Forward<Args>( args )... );
	}

	FORCEINLINE void Destroy() noexcept
	{
		if ( bIsOk ) OkRef().~T();
		else ErrRef().~E();
	}

	FORCEINLINE T& OkRef() noexcept { return *Launder( reinterpret_cast<T*>( Storage ) ); }
	FORCEINLINE const T& OkRef() const noexcept { return *Launder( reinterpret_cast<const T*>( Storage ) ); }
	FORCEINLINE E& ErrRef() noexcept { return *Launder( reinterpret_cast<E*>( Storage ) ); }
	FORCEINLINE const E& ErrRef() const noexcept { return *Launder( reinterpret_cast<const E*>( Storage ) ); }

	// Self の値カテゴリ（lvalue / rvalue）を保ったまま成功値・エラー値を取り出す
	template<typename Self>
	FORCEINLINE static decltype( auto ) ForwardOk( Self&& This )
	{
		if constexpr ( bIsLvalueReference<Self> ) return ( This.OkRef() );
		else return MoveTemp( This.OkRef() );
	}
	template<typename Self>
	FORCEINLINE static decltype( auto ) ForwardErr( Self&& This )
	{
		if constexpr ( bIsLvalueReference<Self> ) return ( This.ErrRef() );
		else return MoveTemp( This.ErrRef() );
	}

	template<typename Self, typename F>
	static auto MapImpl( Self&& This, F&& Func );

	template<typename Self, typename F>
	FORCEINLINE static auto MapErrImpl( Self&& This, F&& Func )
	{
		using E2  = TDecayT<TInvokeResultT<F, decltype( ForwardErr( Forward<Self>( This ) ) )>>;
		using Out = TResult<T, E2>;
		if ( This.IsOk() ) return Out::Ok( ForwardOk( Forward<Self>( This ) ) );
		return Out::Err( Forward<F>( Func )( ForwardErr( Forward<Self>( This ) ) ) );
	}

	template<typename Self, typename F>
	FORCEINLINE static auto AndThenImpl( Self&& This, F&& Func )
	{
		using Out = TDecayT<TInvokeResultT<F, decltype( ForwardOk( Forward<Self>( This ) ) )>>;
		static_assert( NResultDetail::TIsResult<Out>::Value, "AndThen: callback must return TResult" );
		static_assert( bIsSame<typename Out::ErrorType, E>, "AndThen: callback must return the same error type" );
		if ( This.IsErr() ) return Out::Err( ForwardErr( Forward<Self>( This ) ) );
		return Forward<F>( Func )( ForwardOk( Forward<Self>( This ) ) );
	}

	template<typename Self, typename F>
	FORCEINLINE static auto OrElseImpl( Self&& This, F&& Func )
	{
		using Out = TDecayT<TInvokeResultT<F, decltype( ForwardErr( Forward<Self>( This ) ) )>>;
		static_assert( NResultDetail::TIsResult<Out>::Value, "OrElse: callback must return TResult" );
		static_assert( bIsSame<typename Out::ValueType, T>, "OrElse: callback must return the same value type" );
		if ( This.IsOk() ) return Out::Ok( ForwardOk( Forward<Self>( This ) ) );
		return Forward<F>( Func )( ForwardErr( Forward<Self>( This ) ) );
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
	static_assert( !bIsReference<E>, "TResult<void, E&> is not supported" );

public:
	using ValueType = void;
	using ErrorType = E;

	FORCEINLINE static TResult Ok() { return TResult(); }

	template<typename... Args>
	FORCEINLINE static TResult Err( Args&&... args )
	{
		TResult Result;
		Result.Error.Emplace( Forward<Args>( args )... );
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
		return MoveTemp( Error ).GetValue();
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
		using E2  = TDecayT<TInvokeResultT<F, const E&>>;
		using Out = TResult<void, E2>;
		if ( IsOk() ) return Out::Ok();
		return Out::Err( Forward<F>( Func )( Error.GetValue() ) );
	}

	// Ok() -> F() (F は TResult<U, E> を返す)、Err はそのまま
	template<typename F>
	FORCEINLINE auto AndThen( F&& Func ) const
	{
		using Out = TDecayT<TInvokeResultT<F>>;
		static_assert( NResultDetail::TIsResult<Out>::Value, "AndThen: callback must return TResult" );
		static_assert( bIsSame<typename Out::ErrorType, E>, "AndThen: callback must return the same error type" );
		if ( IsErr() ) return Out::Err( Error.GetValue() );
		return Forward<F>( Func )();
	}

	// Err(E) -> F(E) (F は TResult<void, E2> を返す)、Ok はそのまま
	template<typename F>
	FORCEINLINE auto OrElse( F&& Func ) const
	{
		using Out = TDecayT<TInvokeResultT<F, const E&>>;
		static_assert( NResultDetail::TIsResult<Out>::Value, "OrElse: callback must return TResult" );
		static_assert( bIsVoid<typename Out::ValueType>, "OrElse: callback must return TResult<void, ...>" );
		if ( IsOk() ) return Out::Ok();
		return Forward<F>( Func )( Error.GetValue() );
	}

private:
	TResult() = default;

	TOptional<E> Error;
};

#include "Result.inl"
