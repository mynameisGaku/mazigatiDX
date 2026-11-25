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

// 格子点の値から、連続した3次元ノイズと座標に対する勾配を返す。
float MatHash3D( float3 p )
{
	p = frac( p * float3( 0.1031, 0.1030, 0.0973 ) );
	p += dot( p, p.yxz + 33.33 );
	return frac( ( p.x + p.y ) * p.z );
}

float4 MatNoise3DGradient( float3 p )
{
	float3 i = floor( p ); // 格子の原点。
	float3 f = frac( p ); // 格子内の位置。
	float3 u = f * f * ( 3.0 - 2.0 * f ); // 値をなめらかにつなぐ補間率。
	float3 du = 6.0 * f * ( 1.0 - f ); // 補間率の勾配。
	float a = MatHash3D( i ); // 以降の8値は格子の各頂点。
	float b = MatHash3D( i + float3( 1, 0, 0 ) );
	float c = MatHash3D( i + float3( 0, 1, 0 ) );
	float d = MatHash3D( i + float3( 1, 1, 0 ) );
	float e = MatHash3D( i + float3( 0, 0, 1 ) );
	float g = MatHash3D( i + float3( 1, 0, 1 ) );
	float h = MatHash3D( i + float3( 0, 1, 1 ) );
	float j = MatHash3D( i + float3( 1, 1, 1 ) );
	float z0 = lerp( lerp( a, b, u.x ), lerp( c, d, u.x ), u.y ); // 下側の補間値。
	float z1 = lerp( lerp( e, g, u.x ), lerp( h, j, u.x ), u.y ); // 上側の補間値。
	float dx = lerp( lerp( b - a, d - c, u.y ), lerp( g - e, j - h, u.y ), u.z ) * du.x; // X方向の勾配。
	float dy = lerp( lerp( c - a, d - b, u.x ), lerp( h - e, j - g, u.x ), u.z ) * du.y; // Y方向の勾配。
	return float4( lerp( z0, z1, u.z ), dx, dy, ( z1 - z0 ) * du.z );
}

// 凹凸の勾配を局所面へ投影し、逆転置でワールド法線へ変換する。
float3 MatNoise3DNormal( float3 position, float3 localNormal, float strength, row_major float4x4 world, float3 fallback )
{
	float normalLength = dot( localNormal, localNormal ); // 入力法線の長さの二乗。
	float3 n = normalLength > 1.0e-12 ? localNormal * rsqrt( normalLength ) : float3( 0, 1, 0 ); // 単位法線。
	float3 gradient = MatNoise3DGradient( position ).yzw; // ノイズの局所勾配。
	float3 perturbed = n - strength * ( gradient - n * dot( gradient, n ) ); // 面に沿う勾配で傾けた法線。
	float3 x = world[0].xyz, y = world[1].xyz, z = world[2].xyz; // 個体の変換後の局所軸。
	float3 cx = cross( y, z ), cy = cross( z, x ), cz = cross( x, y ); // 逆転置の分子。
	float determinant = dot( x, cx ); // 鏡映と特異変換の判定。
	float3 transformed = ( perturbed.x * cx + perturbed.y * cy + perturbed.z * cz ) * ( determinant < 0.0 ? -1.0 : 1.0 ); // ワールド法線。
	float lengthSquared = dot( transformed, transformed ); // 変換後の長さの二乗。
	float fallbackLength = dot( fallback, fallback ); // 代替法線の長さの二乗。
	float3 safeFallback = fallbackLength > 1.0e-12 ? fallback * rsqrt( fallbackLength ) : float3( 0, 1, 0 ); // 特異変換時の代替。
	return abs( determinant ) > 1.0e-8 && lengthSquared > 1.0e-12 ? transformed * rsqrt( lengthSquared ) : safeFallback;
}
