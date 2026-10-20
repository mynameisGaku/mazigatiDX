#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/Vector4.h"

struct FColor32;

//=============================================================================
// FColor
//   float32 の RGBA（各 0..1、HDR 用に 1 超えも許容）
//=============================================================================
struct alignas( 16 ) FColor
{
	float32 r = 0.0f;
	float32 g = 0.0f;
	float32 b = 0.0f;
	float32 a = 1.0f;

	constexpr FColor() = default;
	FORCEINLINE constexpr FColor( float32 InR, float32 InG, float32 InB, float32 InA = 1.0f ) : r( InR ), g( InG ), b( InB ), a( InA ) {}
	FORCEINLINE constexpr explicit FColor( const FVector4& V ) : r( V.x ), g( V.y ), b( V.z ), a( V.w ) {}

	//-------------------------------------------------------------------------
	// DxLib 変換
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FColor( const COLOR_F& C ) : r( C.r ), g( C.g ), b( C.b ), a( C.a ) {}
	FORCEINLINE constexpr operator COLOR_F() const { return COLOR_F{ r, g, b, a }; }

	// DrawBox などに渡す色 (GetColor)。アルファは無視される。DxLib_Init 後に呼ぶこと
	unsigned int ToDxColor() const;

	FORCEINLINE FString ToString() const { return FString::Printf( "R=%.3f G=%.3f B=%.3f A=%.3f", r, g, b, a ); }

	static const FColor White;
	static const FColor Black;
	static const FColor Gray;
	static const FColor Red;
	static const FColor Green;
	static const FColor Blue;
	static const FColor Yellow;
	static const FColor Cyan;
	static const FColor Magenta;
	static const FColor Orange;
	static const FColor Transparent;

	// 0xRRGGBBAA
	FORCEINLINE static constexpr FColor FromHex( uint32 RGBA )
	{
		return {
			static_cast<float32>( ( RGBA >> 24 ) & 0xFF ) / 255.0f,
			static_cast<float32>( ( RGBA >> 16 ) & 0xFF ) / 255.0f,
			static_cast<float32>( ( RGBA >> 8 ) & 0xFF ) / 255.0f,
			static_cast<float32>( RGBA & 0xFF ) / 255.0f };
	}

	// H: 0..360, S / V: 0..1
	static FColor FromHSV( float32 H, float32 S, float32 V, float32 A = 1.0f );

	FORCEINLINE constexpr FColor operator+( const FColor& C ) const { return { r + C.r, g + C.g, b + C.b, a + C.a }; }
	FORCEINLINE constexpr FColor operator-( const FColor& C ) const { return { r - C.r, g - C.g, b - C.b, a - C.a }; }
	FORCEINLINE constexpr FColor operator*( const FColor& C ) const { return { r * C.r, g * C.g, b * C.b, a * C.a }; }
	FORCEINLINE constexpr FColor operator*( float32 S ) const { return { r * S, g * S, b * S, a * S }; }
	constexpr bool operator==( const FColor& C ) const { return r == C.r && g == C.g && b == C.b && a == C.a; }
	constexpr bool operator!=( const FColor& C ) const { return !( *this == C ); }

	FORCEINLINE constexpr FColor WithAlpha( float32 InA ) const { return { r, g, b, InA }; }
	FORCEINLINE constexpr FColor Clamped() const { return { NMath::Saturate( r ), NMath::Saturate( g ), NMath::Saturate( b ), NMath::Saturate( a ) }; }
	FORCEINLINE constexpr FVector4 ToVector4() const { return { r, g, b, a }; }
	constexpr FColor32 ToColor32() const;

	FORCEINLINE static constexpr FColor Lerp( const FColor& A, const FColor& B, float32 Alpha ) { return A + ( B - A ) * Alpha; }
};

//=============================================================================
// FColor32
//   uint8 の RGBA（各 0..255）
//=============================================================================
struct FColor32
{
	uint8 r = 0;
	uint8 g = 0;
	uint8 b = 0;
	uint8 a = 255;

	constexpr FColor32() = default;
	FORCEINLINE constexpr FColor32( uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255 ) : r( InR ), g( InG ), b( InB ), a( InA ) {}

	//-------------------------------------------------------------------------
	// DxLib 変換（COLOR_U8 のメンバ順は b, g, r, a）
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FColor32( const COLOR_U8& C ) : r( C.r ), g( C.g ), b( C.b ), a( C.a ) {}
	FORCEINLINE constexpr operator COLOR_U8() const { return COLOR_U8{ b, g, r, a }; }

	// DrawBox などに渡す色 (GetColor)。アルファは無視される。DxLib_Init 後に呼ぶこと
	FORCEINLINE unsigned int ToDxColor() const { return GetColor( r, g, b ); }

	// GetColor の値から復元（アルファは 255）
	FORCEINLINE static FColor32 FromDxColor( unsigned int DxColor )
	{
		int R = 0, G = 0, B = 0;
		GetColor2( DxColor, &R, &G, &B );
		return { static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), 255 };
	}

	FORCEINLINE FString ToString() const { return FString::Printf( "R=%u G=%u B=%u A=%u", r, g, b, a ); }

	static const FColor32 White;
	static const FColor32 Black;
	static const FColor32 Gray;
	static const FColor32 Red;
	static const FColor32 Green;
	static const FColor32 Blue;
	static const FColor32 Yellow;
	static const FColor32 Cyan;
	static const FColor32 Magenta;
	static const FColor32 Orange;
	static const FColor32 Transparent;

	// 0xRRGGBBAA
	FORCEINLINE static constexpr FColor32 FromHex( uint32 RGBA )
	{
		return {
			static_cast<uint8>( ( RGBA >> 24 ) & 0xFF ),
			static_cast<uint8>( ( RGBA >> 16 ) & 0xFF ),
			static_cast<uint8>( ( RGBA >> 8 ) & 0xFF ),
			static_cast<uint8>( RGBA & 0xFF ) };
	}

	// 0xRRGGBBAA
	FORCEINLINE constexpr uint32 ToRGBA() const { return ( uint32( r ) << 24 ) | ( uint32( g ) << 16 ) | ( uint32( b ) << 8 ) | uint32( a ); }
	// 0xAARRGGBB
	FORCEINLINE constexpr uint32 ToARGB() const { return ( uint32( a ) << 24 ) | ( uint32( r ) << 16 ) | ( uint32( g ) << 8 ) | uint32( b ); }

	constexpr bool operator==( const FColor32& C ) const { return r == C.r && g == C.g && b == C.b && a == C.a; }
	constexpr bool operator!=( const FColor32& C ) const { return !( *this == C ); }

	FORCEINLINE constexpr FColor ToColor() const
	{
		return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
	}
};

FORCEINLINE constexpr FColor32 FColor::ToColor32() const
{
	const FColor C = Clamped();
	return {
		static_cast<uint8>( C.r * 255.0f + 0.5f ),
		static_cast<uint8>( C.g * 255.0f + 0.5f ),
		static_cast<uint8>( C.b * 255.0f + 0.5f ),
		static_cast<uint8>( C.a * 255.0f + 0.5f ) };
}

FORCEINLINE unsigned int FColor::ToDxColor() const { return ToColor32().ToDxColor(); }

FORCEINLINE constexpr FColor operator*( float32 S, const FColor& C ) { return C * S; }

inline constexpr FColor FColor::White       { 1.0f, 1.0f, 1.0f, 1.0f };
inline constexpr FColor FColor::Black       { 0.0f, 0.0f, 0.0f, 1.0f };
inline constexpr FColor FColor::Gray        { 0.5f, 0.5f, 0.5f, 1.0f };
inline constexpr FColor FColor::Red         { 1.0f, 0.0f, 0.0f, 1.0f };
inline constexpr FColor FColor::Green       { 0.0f, 1.0f, 0.0f, 1.0f };
inline constexpr FColor FColor::Blue        { 0.0f, 0.0f, 1.0f, 1.0f };
inline constexpr FColor FColor::Yellow      { 1.0f, 1.0f, 0.0f, 1.0f };
inline constexpr FColor FColor::Cyan        { 0.0f, 1.0f, 1.0f, 1.0f };
inline constexpr FColor FColor::Magenta     { 1.0f, 0.0f, 1.0f, 1.0f };
inline constexpr FColor FColor::Orange      { 1.0f, 0.5f, 0.0f, 1.0f };
inline constexpr FColor FColor::Transparent { 0.0f, 0.0f, 0.0f, 0.0f };

inline constexpr FColor32 FColor32::White       { 255, 255, 255, 255 };
inline constexpr FColor32 FColor32::Black       {   0,   0,   0, 255 };
inline constexpr FColor32 FColor32::Gray        { 128, 128, 128, 255 };
inline constexpr FColor32 FColor32::Red         { 255,   0,   0, 255 };
inline constexpr FColor32 FColor32::Green       {   0, 255,   0, 255 };
inline constexpr FColor32 FColor32::Blue        {   0,   0, 255, 255 };
inline constexpr FColor32 FColor32::Yellow      { 255, 255,   0, 255 };
inline constexpr FColor32 FColor32::Cyan        {   0, 255, 255, 255 };
inline constexpr FColor32 FColor32::Magenta     { 255,   0, 255, 255 };
inline constexpr FColor32 FColor32::Orange      { 255, 128,   0, 255 };
inline constexpr FColor32 FColor32::Transparent {   0,   0,   0,   0 };
