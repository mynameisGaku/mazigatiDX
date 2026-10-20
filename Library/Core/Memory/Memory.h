#pragma once
#include <malloc.h>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FMemory
//   メモリ確保・コピーの低レベル関数。コンテナやスマートポインタの土台。
//=============================================================================
struct FMemory
{
	// 最低限保証するアライメント
	static constexpr usize MinAlignment = alignof( std::max_align_t );

	FORCEINLINE static void* Malloc( usize Size, usize Alignment = MinAlignment )
	{
		return _aligned_malloc( Size > 0 ? Size : 1, Alignment < MinAlignment ? MinAlignment : Alignment );
	}

	FORCEINLINE static void* Realloc( void* Ptr, usize Size, usize Alignment = MinAlignment )
	{
		return _aligned_realloc( Ptr, Size > 0 ? Size : 1, Alignment < MinAlignment ? MinAlignment : Alignment );
	}

	FORCEINLINE static void Free( void* Ptr )
	{
		_aligned_free( Ptr );
	}

	FORCEINLINE static void Memcpy( void* Dest, const void* Src, usize Size ) { if ( Size > 0 ) std::memcpy( Dest, Src, Size ); }
	FORCEINLINE static void Memmove( void* Dest, const void* Src, usize Size ) { if ( Size > 0 ) std::memmove( Dest, Src, Size ); }
	FORCEINLINE static void Memzero( void* Dest, usize Size ) { if ( Size > 0 ) std::memset( Dest, 0, Size ); }
	FORCEINLINE static int32 Memcmp( const void* A, const void* B, usize Size ) { return Size > 0 ? std::memcmp( A, B, Size ) : 0; }

	// T 型 Count 個分の未初期化メモリ
	template<typename T>
	FORCEINLINE static T* AllocateArray( int32 Count )
	{
		return static_cast<T*>( Malloc( sizeof( T ) * static_cast<usize>( Count ), alignof( T ) ) );
	}
};

//=============================================================================
// 要素の構築・破棄・移動（未初期化メモリ上で使う）
//=============================================================================
namespace NMemoryOps
{
	template<typename T>
	inline constexpr bool bIsBitwiseRelocatable = std::is_trivially_copyable_v<T>;

	// 値初期化で Count 個構築（POD はゼロ）
	template<typename T>
	FORCEINLINE void DefaultConstructItems( T* Dest, int32 Count )
	{
		for ( int32 i = 0; i < Count; ++i ) new ( Dest + i ) T();
	}

	template<typename T>
	FORCEINLINE void CopyConstructItems( T* Dest, const T* Src, int32 Count )
	{
		if constexpr ( std::is_trivially_copyable_v<T> )
		{
			FMemory::Memcpy( Dest, Src, sizeof( T ) * static_cast<usize>( Count ) );
		}
		else
		{
			for ( int32 i = 0; i < Count; ++i ) new ( Dest + i ) T( Src[i] );
		}
	}

	template<typename T>
	FORCEINLINE void DestructItems( T* Items, int32 Count )
	{
		if constexpr ( !std::is_trivially_destructible_v<T> )
		{
			for ( int32 i = 0; i < Count; ++i ) Items[i].~T();
		}
	}

	template<typename T>
	FORCEINLINE void DestructItem( T* Item )
	{
		if constexpr ( !std::is_trivially_destructible_v<T> ) Item->~T();
	}

	// Src から Dest（未初期化・重ならない）へ移動し、Src を破棄する
	template<typename T>
	void RelocateItems( T* Dest, T* Src, int32 Count );
}

#include "Memory.inl"
