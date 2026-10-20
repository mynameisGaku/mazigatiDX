#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/Hash.h"

//=============================================================================
// TDefaultDelete
//=============================================================================
template<typename T>
struct TDefaultDelete
{
	TDefaultDelete() = default;

	// TDefaultDelete<Derived> -> TDefaultDelete<Base>
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TDefaultDelete( const TDefaultDelete<U>& ) {}

	FORCEINLINE void operator()( T* Ptr ) const
	{
		static_assert( sizeof( T ) > 0, "Cannot delete an incomplete type" );
		delete Ptr;
	}
};

template<typename T>
struct TDefaultDelete<T[]>
{
	FORCEINLINE void operator()( T* Ptr ) const
	{
		static_assert( sizeof( T ) > 0, "Cannot delete an incomplete type" );
		delete[] Ptr;
	}
};

//=============================================================================
// TUniquePtr<T>
//   単独所有のスマートポインタ。コピー不可・ムーブ可。スコープを抜けると自動で delete。
//
//   TUniquePtr<CEnemy> Enemy = MakeUnique<CEnemy>( Hp );
//   Enemy->Tick( dt );
//   TUniquePtr<CEnemy> Other = std::move( Enemy );   // 所有権の移動
//
//   Deleter は空のクラス型（関数オブジェクト）を想定している。
//=============================================================================
template<typename T, typename Deleter = TDefaultDelete<T>>
class TUniquePtr : private Deleter  // 空の Deleter でサイズが増えないよう継承で保持
{
	template<typename, typename>
	friend class TUniquePtr;

public:
	using ElementType = T;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	TUniquePtr() noexcept = default;
	FORCEINLINE TUniquePtr( decltype( nullptr ) ) noexcept {}
	FORCEINLINE explicit TUniquePtr( T* InPtr ) noexcept : Ptr( InPtr ) {}
	FORCEINLINE TUniquePtr( T* InPtr, const Deleter& InDeleter ) noexcept : Deleter( InDeleter ), Ptr( InPtr ) {}

	TUniquePtr( const TUniquePtr& ) = delete;
	TUniquePtr& operator=( const TUniquePtr& ) = delete;

	TUniquePtr( TUniquePtr&& Other ) noexcept
		: Deleter( std::move( Other.GetDeleter() ) ), Ptr( Other.Ptr )
	{
		Other.Ptr = nullptr;
	}

	// TUniquePtr<Derived> -> TUniquePtr<Base>
	template<typename U, typename E, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TUniquePtr( TUniquePtr<U, E>&& Other ) noexcept
		: Deleter( std::move( Other.GetDeleter() ) ), Ptr( Other.Ptr )
	{
		Other.Ptr = nullptr;
	}

	FORCEINLINE ~TUniquePtr() { Reset(); }

	FORCEINLINE TUniquePtr& operator=( TUniquePtr&& Other ) noexcept
	{
		if ( this != &Other )
		{
			Reset( Other.Release() );
			GetDeleter() = std::move( Other.GetDeleter() );
		}
		return *this;
	}

	template<typename U, typename E, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TUniquePtr& operator=( TUniquePtr<U, E>&& Other ) noexcept
	{
		Reset( Other.Release() );
		GetDeleter() = std::move( Other.GetDeleter() );
		return *this;
	}

	FORCEINLINE TUniquePtr& operator=( decltype( nullptr ) ) noexcept
	{
		Reset();
		return *this;
	}

	//-------------------------------------------------------------------------
	// 操作
	//-------------------------------------------------------------------------
	// 保持中のオブジェクトを破棄し、NewPtr を保持する
	FORCEINLINE void Reset( T* NewPtr = nullptr ) noexcept
	{
		T* Old = Ptr;
		Ptr = NewPtr;
		if ( Old ) GetDeleter()( Old );
	}

	// 所有権を手放して生ポインタを返す（delete は呼び出し側の責任）
	[[nodiscard]] FORCEINLINE T* Release() noexcept
	{
		T* Old = Ptr;
		Ptr = nullptr;
		return Old;
	}

	FORCEINLINE T* Get() const noexcept { return Ptr; }
	FORCEINLINE bool IsValid() const noexcept { return Ptr != nullptr; }
	FORCEINLINE explicit operator bool() const noexcept { return Ptr != nullptr; }

	FORCEINLINE T& operator*() const { return *Ptr; }
	FORCEINLINE T* operator->() const noexcept { return Ptr; }

	FORCEINLINE Deleter& GetDeleter() noexcept { return *this; }
	FORCEINLINE const Deleter& GetDeleter() const noexcept { return *this; }

	//-------------------------------------------------------------------------
	// 比較
	//-------------------------------------------------------------------------
	template<typename U, typename E>
	bool operator==( const TUniquePtr<U, E>& Other ) const noexcept { return Ptr == Other.Get(); }
	template<typename U, typename E>
	bool operator!=( const TUniquePtr<U, E>& Other ) const noexcept { return Ptr != Other.Get(); }
	bool operator==( decltype( nullptr ) ) const noexcept { return Ptr == nullptr; }
	bool operator!=( decltype( nullptr ) ) const noexcept { return Ptr != nullptr; }

	friend FORCEINLINE uint32 GetTypeHash( const TUniquePtr& P ) { return GetTypeHash( P.Ptr ); }

private:
	T* Ptr = nullptr;
};

//=============================================================================
// TUniquePtr<T[]>（配列版）
//=============================================================================
template<typename T, typename Deleter>
class TUniquePtr<T[], Deleter> : private Deleter
{
public:
	using ElementType = T;

	TUniquePtr() noexcept = default;
	FORCEINLINE TUniquePtr( decltype( nullptr ) ) noexcept {}
	FORCEINLINE explicit TUniquePtr( T* InPtr ) noexcept : Ptr( InPtr ) {}

	TUniquePtr( const TUniquePtr& ) = delete;
	TUniquePtr& operator=( const TUniquePtr& ) = delete;

	FORCEINLINE TUniquePtr( TUniquePtr&& Other ) noexcept : Deleter( std::move( Other.GetDeleter() ) ), Ptr( Other.Ptr ) { Other.Ptr = nullptr; }
	FORCEINLINE TUniquePtr& operator=( TUniquePtr&& Other ) noexcept
	{
		if ( this != &Other )
		{
			Reset( Other.Release() );
			GetDeleter() = std::move( Other.GetDeleter() );
		}
		return *this;
	}

	FORCEINLINE ~TUniquePtr() { Reset(); }

	FORCEINLINE void Reset( T* NewPtr = nullptr ) noexcept
	{
		T* Old = Ptr;
		Ptr = NewPtr;
		if ( Old ) GetDeleter()( Old );
	}

	[[nodiscard]] FORCEINLINE T* Release() noexcept
	{
		T* Old = Ptr;
		Ptr = nullptr;
		return Old;
	}

	FORCEINLINE T* Get() const noexcept { return Ptr; }
	FORCEINLINE bool IsValid() const noexcept { return Ptr != nullptr; }
	FORCEINLINE explicit operator bool() const noexcept { return Ptr != nullptr; }
	T& operator[]( int32 Index ) const { return Ptr[Index]; }

	FORCEINLINE Deleter& GetDeleter() noexcept { return *this; }
	FORCEINLINE const Deleter& GetDeleter() const noexcept { return *this; }

	bool operator==( decltype( nullptr ) ) const noexcept { return Ptr == nullptr; }
	bool operator!=( decltype( nullptr ) ) const noexcept { return Ptr != nullptr; }

private:
	T* Ptr = nullptr;
};

//=============================================================================
// MakeUnique
//   auto P = MakeUnique<CEnemy>( Args... );
//   auto A = MakeUnique<int32[]>( 16 );   // 値初期化された配列
//=============================================================================
template<typename T, typename... Args, typename = std::enable_if_t<!std::is_array_v<T>>>
FORCEINLINE TUniquePtr<T> MakeUnique( Args&&... args )
{
	return TUniquePtr<T>( new T( std::forward<Args>( args )... ) );
}

template<typename T, typename = std::enable_if_t<std::is_array_v<T> && std::extent_v<T> == 0>>
FORCEINLINE TUniquePtr<T> MakeUnique( int32 Count )
{
	using ElementT = std::remove_extent_t<T>;
	return TUniquePtr<T>( new ElementT[static_cast<usize>( Count )]() );
}
