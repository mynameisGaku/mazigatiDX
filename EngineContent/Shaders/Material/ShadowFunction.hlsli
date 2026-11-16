// SPDX-License-Identifier: MIT

float MatShadow( float3 world, float3 normal )
{
	float4 clip = mul( float4( world + normal * g_ShadowOptions.z, 1.0 ), g_ShadowViewProjection );
	float3 position = clip.xyz / clip.w;
	if ( position.z <= 0.0 || position.z >= 1.0 )
	{
		return 1.0;
	}
	float size = g_ShadowOptions.x;
	float2 texel = float2( position.x * 0.5 + 0.5, 0.5 - position.y * 0.5 ) * size - 0.5;
	float2 base = floor( texel );
	float2 fraction = texel - base;
	float sum = 0.0;
	for ( int y = -1; y <= 2; ++y )
	{
		for ( int x = -1; x <= 2; ++x )
		{
			float2 tap = base + float2( x, y );
			float lit = 1.0;
			if ( tap.x >= 0.0 && tap.y >= 0.0 && tap.x < size && tap.y < size )
			{
				lit = position.z - g_ShadowOptions.y <= g_ShadowMap.Load( int3( ( int )tap.x, ( int )tap.y, 0 ) ).r ? 1.0 : 0.0;
			}
			float weightX = x == -1 ? 1.0 - fraction.x : ( x == 2 ? fraction.x : 1.0 );
			float weightY = y == -1 ? 1.0 - fraction.y : ( y == 2 ? fraction.y : 1.0 );
			sum += lit * weightX * weightY;
		}
	}
	return sum / 9.0;
}
