#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <new>
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/CollectionCommon.h"

// 「値なし」を表すタグ:  TOptional<int32> X = NullOpt;
struct FNullOpt
{
	FORCEINLINE explicit constexpr FNullOpt( int ) {}
};
inline constexpr FNullOpt NullOpt { 0 };

//=============================================================================
// TOptional<T>
//   値を持つか持たないかを表す。値は内部のバッファに直接置かれる（ヒープ確保なし）。
//
//   TOptional<int32> Index = Str.TryParseInt();
//   if ( Index.IsSet() ) Use( Index.GetValue() );
//   int32 i = Index.Get( -1 );   // 無ければ -1
//=============================================================================
template<typename T>
class TOptional
{
	static_assert( !std::is_reference_v<T>, "TOptional<T&> is not supported" );

public:
	using ElementType = T;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	TOptional() noexcept = default;
	FORCEINLINE TOptional( FNullOpt ) noexcept {}
	FORCEINLINE TOptional( const T& InValue ) { Emplace( InValue ); }
	FORCEINLINE TOptional( T&& InValue ) { Emplace( std::move( InValue ) ); }

	FORCEINLINE TOptional( const TOptional& Other )
	{
		if ( Other.bIsSet ) Emplace( Other.Ref() );
	}
	FORCEINLINE TOptional( TOptional&& Other ) noexcept( std::is_nothrow_move_constructible_v<T> )
	{
		if ( Other.bIsSet ) Emplace( std::move( Other.Ref() ) );
	}

	FORCEINLINE ~TOptional() { Reset(); }

	TOptional& operator=( const TOptional& Other );

	TOptional& operator=( TOptional&& Other ) noexcept( std::is_nothrow_move_assignable_v<T> );

	FORCEINLINE TOptional& operator=( const T& InValue )
	{
		if ( bIsSet ) Ref() = InValue;
		else Emplace( InValue );
		return *this;
	}
	FORCEINLINE TOptional& operator=( T&& InValue )
	{
		if ( bIsSet ) Ref() = std::move( InValue );
		else Emplace( std::move( InValue ) );
		return *this;
	}
	TOptional& operator=( FNullOpt ) noexcept { Reset(); return *this; }

	//-------------------------------------------------------------------------
	// 状態
	//-------------------------------------------------------------------------
	FORCEINLINE bool IsSet() const noexcept { return bIsSet; }
	FORCEINLINE explicit operator bool() const noexcept { return bIsSet; }

	//-------------------------------------------------------------------------
	// 取得
	//-------------------------------------------------------------------------
	// 未設定で呼ぶと assert
	FORCEINLINE T& GetValue() &
	{
		COLLECTION_CHECK( bIsSet, "TOptional::GetValue() called on unset optional" );
		return Ref();
	}
	FORCEINLINE const T& GetValue() const&
	{
		COLLECTION_CHECK( bIsSet, "TOptional::GetValue() called on unset optional" );
		return Ref();
	}
	FORCEINLINE T&& GetValue() &&
	{
		COLLECTION_CHECK( bIsSet, "TOptional::GetValue() called on unset optional" );
		return std::move( Ref() );
	}

	// 未設定なら Default を返す
	template<typename U>
	FORCEINLINE T Get( U&& Default ) const& { return bIsSet ? Ref() : static_cast<T>( std::forward<U>( Default ) ); }
	template<typename U>
	FORCEINLINE T Get( U&& Default ) && { return bIsSet ? std::move( Ref() ) : static_cast<T>( std::forward<U>( Default ) ); }

	// 値へのポインタ（未設定なら nullptr）
	FORCEINLINE T* GetPtrOrNull() noexcept { return bIsSet ? &Ref() : nullptr; }
	FORCEINLINE const T* GetPtrOrNull() const noexcept { return bIsSet ? &Ref() : nullptr; }

	FORCEINLINE T& operator*() & { return GetValue(); }
	FORCEINLINE const T& operator*() const& { return GetValue(); }
	FORCEINLINE T&& operator*() && { return std::move( *this ).GetValue(); }
	FORCEINLINE T* operator->() { return &GetValue(); }
	FORCEINLINE const T* operator->() const { return &GetValue(); }

	//-------------------------------------------------------------------------
	// 変更
	//-------------------------------------------------------------------------
	template<typename... Args>
	FORCEINLINE T& Emplace( Args&&... args )
	{
		Reset();
		new ( Storage ) T( std::forward<Args>( args )... );
		bIsSet = true;
		return Ref();
	}

	FORCEINLINE void Reset() noexcept
	{
		if ( bIsSet )
		{
			Ref().~T();
			bIsSet = false;
		}
	}

	//-------------------------------------------------------------------------
	// 比較
	//-------------------------------------------------------------------------
	friend FORCEINLINE bool operator==( const TOptional& A, const TOptional& B )
	{
		if ( A.bIsSet != B.bIsSet ) return false;
		return !A.bIsSet || A.Ref() == B.Ref();
	}
	friend bool operator!=( const TOptional& A, const TOptional& B ) { return !( A == B ); }

private:
	FORCEINLINE T& Ref() noexcept { return *std::launder( reinterpret_cast<T*>( Storage ) ); }
	FORCEINLINE const T& Ref() const noexcept { return *std::launder( reinterpret_cast<const T*>( Storage ) ); }

	alignas( T ) unsigned char Storage[sizeof( T )];
	bool bIsSet = false;
};

// 型推論付きで作る: auto Opt = MakeOptional( 3 );
template<typename T>
FORCEINLINE TOptional<std::decay_t<T>> MakeOptional( T&& InValue )
{
	return TOptional<std::decay_t<T>>( std::forward<T>( InValue ) );
}

#include "Optional.inl"
