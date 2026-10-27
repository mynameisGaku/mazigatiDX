#pragma once

#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"

//=============================================================================
// ハッシュ関数
//   TMap / TSet のキーには GetTypeHash( const T& ) -> uint32 が必要。
//   独自型をキーにするときは同じ名前空間に GetTypeHash を定義する:
//
//   struct FGridPos { int32 X, Y; bool operator==( const FGridPos& ) const; };
//   inline uint32 GetTypeHash( const FGridPos& P ) { return HashCombine( GetTypeHash( P.X ), GetTypeHash( P.Y ) ); }
//=============================================================================

// ビットをよく混ぜる（MurmurHash3 の finalizer）
FORCEINLINE constexpr uint32 MurmurFinalize32( uint32 Hash )
{
	Hash ^= Hash >> 16;
	Hash *= 0x85EBCA6Bu;
	Hash ^= Hash >> 13;
	Hash *= 0xC2B2AE35u;
	Hash ^= Hash >> 16;
	return Hash;
}

FORCEINLINE constexpr uint32 MurmurFinalize64( uint64 Hash )
{
	Hash ^= Hash >> 33;
	Hash *= 0xFF51AFD7ED558CCDull;
	Hash ^= Hash >> 33;
	Hash *= 0xC4CEB9FE1A85EC53ull;
	Hash ^= Hash >> 33;
	return static_cast<uint32>( Hash );
}

// 2 つのハッシュを合成
FORCEINLINE constexpr uint32 HashCombine( uint32 A, uint32 B )
{
	return A ^ ( B + 0x9E3779B9u + ( A << 6 ) + ( A >> 2 ) );
}

// バイト列のハッシュ (FNV-1a)
FORCEINLINE constexpr uint32 HashBytes( const void* Data, usize Size )
{
	const unsigned char* Bytes = static_cast<const unsigned char*>( Data );
	uint32 Hash = 2166136261u;
	for ( usize i = 0; i < Size; ++i )
	{
		Hash ^= Bytes[i];
		Hash *= 16777619u;
	}
	return Hash;
}

//-----------------------------------------------------------------------------
// 基本型
//-----------------------------------------------------------------------------
FORCEINLINE constexpr uint32 GetTypeHash( int8 Value )   { return MurmurFinalize32( static_cast<uint32>( static_cast<uint8>( Value ) ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( uint8 Value )  { return MurmurFinalize32( Value ); }
FORCEINLINE constexpr uint32 GetTypeHash( int16 Value )  { return MurmurFinalize32( static_cast<uint32>( static_cast<uint16>( Value ) ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( uint16 Value ) { return MurmurFinalize32( Value ); }
FORCEINLINE constexpr uint32 GetTypeHash( int32 Value )  { return MurmurFinalize32( static_cast<uint32>( Value ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( uint32 Value ) { return MurmurFinalize32( Value ); }
FORCEINLINE constexpr uint32 GetTypeHash( int64 Value )  { return MurmurFinalize64( static_cast<uint64>( Value ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( uint64 Value ) { return MurmurFinalize64( Value ); }
FORCEINLINE constexpr uint32 GetTypeHash( char Value )   { return MurmurFinalize32( static_cast<uint32>( static_cast<unsigned char>( Value ) ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( wchar_t Value ) { return MurmurFinalize32( static_cast<uint32>( Value ) ); }
FORCEINLINE constexpr uint32 GetTypeHash( bool Value )   { return Value ? 1u : 0u; }

FORCEINLINE uint32 GetTypeHash( float32 Value )
{
	if ( Value == 0.0f ) Value = 0.0f;  // -0 と +0 を同一視
	uint32 Bits = 0;
	static_assert( sizeof( Bits ) == sizeof( Value ) );
	FMemory::Memcpy( &Bits, &Value, sizeof( Bits ) );
	return MurmurFinalize32( Bits );
}

FORCEINLINE uint32 GetTypeHash( float64 Value )
{
	if ( Value == 0.0 ) Value = 0.0;
	uint64 Bits = 0;
	static_assert( sizeof( Bits ) == sizeof( Value ) );
	FMemory::Memcpy( &Bits, &Value, sizeof( Bits ) );
	return MurmurFinalize64( Bits );
}

// ポインタ（アドレスのハッシュ。文字列の中身ではない）
template<typename T>
FORCEINLINE uint32 GetTypeHash( T* Ptr )
{
	return MurmurFinalize64( static_cast<uint64>( reinterpret_cast<uintptr>( Ptr ) ) );
}

// enum
template<typename T, typename = TEnableIfT<bIsEnum<T>>>
FORCEINLINE constexpr uint32 GetTypeHash( T Value )
{
	return GetTypeHash( static_cast<TUnderlyingTypeT<T>>( Value ) );
}

//-----------------------------------------------------------------------------
// 既定のハッシャー（GetTypeHash を ADL で呼ぶ）
//-----------------------------------------------------------------------------
template<typename T>
struct TDefaultHasher
{
	FORCEINLINE uint32 operator()( const T& Value ) const { return GetTypeHash( Value ); }
};
