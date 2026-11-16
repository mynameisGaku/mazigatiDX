// SPDX-License-Identifier: MIT

#define MatMod( a, b ) ( ( a ) - ( b ) * floor( ( a ) / ( b ) ) )

float MatHash( float2 p )
{
	p = frac( p * float2( 123.34, 456.21 ) );
	p += dot( p, p + 45.32 );
	return frac( p.x * p.y );
}

float MatNoise( float2 p )
{
	float2 i = floor( p );
	float2 f = frac( p );
	float a = MatHash( i );
	float b = MatHash( i + float2( 1.0, 0.0 ) );
	float c = MatHash( i + float2( 0.0, 1.0 ) );
	float d = MatHash( i + float2( 1.0, 1.0 ) );
	float2 u = f * f * ( 3.0 - 2.0 * f );
	return lerp( lerp( a, b, u.x ), lerp( c, d, u.x ), u.y );
}
