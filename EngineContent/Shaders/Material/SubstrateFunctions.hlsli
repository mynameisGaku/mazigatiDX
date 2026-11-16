// SPDX-License-Identifier: MIT

// ---- Substrate (the full model). The same math as NMaterialSubstrate::ShadeSlab ----
struct SubstrateSlab
{
	float3 BaseColor;
	float3 F0;
	float Roughness;
	float Metallic;
	float3 Emissive;
	float ClearCoat;
	float ClearCoatRoughness;
	float3 Subsurface;
	float Anisotropy;
	float ThinFilm;
	float3 Normal;         // the normal of this slab (world). Zero: the normal of the surface
	float3 Throughput;     // how much of this slab shows through the slabs above it
	float3 DiffuseScale;   // how much of the light scatters inside this slab
};

struct SubstrateGeometry
{
	float3 Normal;
	float3 Tangent;
	float3 ToLight;
	float3 ToCamera;
	float3 Position;
};

struct SubstrateLighting
{
	float3 LightColor;
	float Shadow;
	float3 SkyColor;
	float3 GroundColor;
	float3 AmbientColor;
	int NumPointLights;
	float4 PointLightPositions[4];   // xyz, w = radius
	float4 PointLightColors[4];
	float4 Environment;              // the environment atlas: x = width of a level, y = height of a level, z = number of levels, w = intensity
};

// What depends on the view only (shared by every light)
struct SubstrateSurface
{
	float3 N;
	float3 V;
	float3 T;
	float3 B;
	float NdotV;
	float3 SpecularColor;
	float AlphaT;
	float AlphaB;
	float3 DiffuseColor;
	float3 SubsurfaceColor;
	float Coat;
	float CoatAlpha;
};

float3 SubstrateSafeNormalize( float3 v, float3 fallback )
{
	float lengthSquared = dot( v, v );
	return lengthSquared > 1.0e-12 ? v * rsqrt( lengthSquared ) : fallback;
}

// A tangent perpendicular to the normal (an arbitrary one when the given tangent is degenerate)
float3 SubstrateMakeTangent( float3 N, float3 tangent )
{
	float3 other = abs( N.y ) < 0.99 ? cross( float3( 0.0, 1.0, 0.0 ), N ) : float3( 1.0, 0.0, 0.0 );
	float3 fallback = SubstrateSafeNormalize( other - N * dot( N, other ), float3( 1.0, 0.0, 0.0 ) );
	return SubstrateSafeNormalize( tangent - N * dot( N, tangent ), fallback );
}

// The tangent of the pixel (the direction in which U grows), from the screen-space derivatives of the position and the UV
float3 SubstrateTangent( float3 N, float3 worldPosition, float2 uv )
{
	float3 dpx = ddx( worldPosition );
	float3 dpy = ddy( worldPosition );
	float2 dux = ddx( uv );
	float2 duy = ddy( uv );
	float side = dux.x * duy.y - dux.y * duy.x < 0.0 ? -1.0 : 1.0;
	return SubstrateMakeTangent( N, ( dpx * duy.y - dpy * dux.y ) * side );
}

// The bitangent of the pixel (the direction in which V grows)
float3 SubstrateBitangent( float3 N, float3 worldPosition, float2 uv )
{
	float3 dpx = ddx( worldPosition );
	float3 dpy = ddy( worldPosition );
	float2 dux = ddx( uv );
	float2 duy = ddy( uv );
	float side = dux.x * duy.y - dux.y * duy.x < 0.0 ? -1.0 : 1.0;
	return SubstrateMakeTangent( N, ( dpy * dux.x - dpx * duy.x ) * side );
}

float SubstratePow5( float v )
{
	float squared = v * v;
	return squared * squared * v;
}

float SubstrateSpecularLobe( float ndh, float ndl, float ndv, float alpha )
{
	float alphaSquared = alpha * alpha;
	float denominator = ndh * ndh * ( alphaSquared - 1.0 ) + 1.0;
	float distribution = alphaSquared / ( 3.14159265 * denominator * denominator );
	float visibility = 0.5 / max( lerp( 2.0 * ndl * ndv, ndl + ndv, alpha ), 1.0e-4 );
	return min( distribution * visibility * 3.14159265, 16.0 );
}

#ifdef SUBSTRATE_ENVIRONMENT_MAP
// One level of the environment atlas, read with a bilinear filter (wraps around sideways, stops at the top and the bottom)
float3 SubstrateEnvironmentLevel( float3 direction, int level, float4 environment )
{
	int width = ( int )environment.x;
	int height = ( int )environment.y;
	float azimuth = abs( direction.x ) + abs( direction.z ) < 1.0e-6 ? 0.0 : atan2( direction.x, direction.z );
	float x = ( azimuth / ( 2.0 * 3.14159265 ) + 0.5 ) * environment.x - 0.5;
	float y = acos( clamp( direction.y, -1.0, 1.0 ) ) / 3.14159265 * environment.y - 0.5;
	float floorX = floor( x );
	float floorY = floor( y );
	int x0 = ( ( ( int )floorX % width ) + width ) % width;
	int x1 = ( x0 + 1 ) % width;
	int y0 = clamp( ( int )floorY, 0, height - 1 ) + level * height;
	int y1 = clamp( ( int )floorY + 1, 0, height - 1 ) + level * height;
	float3 top = lerp( SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x0, y0, 0 ) ).rgb, SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x1, y0, 0 ) ).rgb, x - floorX );
	float3 bottom = lerp( SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x0, y1, 0 ) ).rgb, SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x1, y1, 0 ) ).rgb, x - floorX );
	return lerp( top, bottom, y - floorY );
}
#endif

// The color of the environment in a direction (unit length). blur: 0 = a mirror, 1 = everything around
float3 SubstrateEnvironment( float3 direction, float blur, SubstrateLighting lighting )
{
#ifdef SUBSTRATE_ENVIRONMENT_MAP
	float position = saturate( blur ) * ( lighting.Environment.z - 1.0 );
	float lower = floor( position );
	int level0 = ( int )lower;
	int level1 = min( level0 + 1, ( int )lighting.Environment.z - 1 );
	return lerp( SubstrateEnvironmentLevel( direction, level0, lighting.Environment ), SubstrateEnvironmentLevel( direction, level1, lighting.Environment ), position - lower ) * lighting.Environment.w;
#else
	float3 gradient = lerp( lighting.GroundColor, lighting.SkyColor, saturate( direction.y * 0.5 + 0.5 ) );
	return lerp( gradient, lighting.AmbientColor, saturate( blur ) );
#endif
}

// Adds one light (direction L, brightness radiance): direct = the slab itself, coatDirect = the highlight of the clear coat
void SubstrateAddLight( SubstrateSurface s, float3 L, float3 radiance, inout float3 direct, inout float3 coatDirect )
{
	float3 H = SubstrateSafeNormalize( L + s.V, s.N );
	float ndlRaw = dot( s.N, L );
	float ndl = saturate( ndlRaw );
	float ndh = saturate( dot( s.N, H ) );
	float vdh = saturate( dot( s.V, H ) );
	float3 fresnel = s.SpecularColor + ( 1.0 - s.SpecularColor ) * SubstratePow5( 1.0 - vdh );

	float tdh = dot( s.T, H );
	float bdh = dot( s.B, H );
	float denominator = tdh * tdh / ( s.AlphaT * s.AlphaT ) + bdh * bdh / ( s.AlphaB * s.AlphaB ) + ndh * ndh;
	float distribution = 1.0 / ( 3.14159265 * s.AlphaT * s.AlphaB * denominator * denominator );
	float visibility = 0.5 / max( lerp( 2.0 * ndl * s.NdotV, ndl + s.NdotV, sqrt( s.AlphaT * s.AlphaB ) ), 1.0e-4 );
	float3 specular = fresnel * ( min( distribution * visibility * 3.14159265, 16.0 ) * ndl );

	float3 diffuse = s.DiffuseColor * ( 1.0 - fresnel ) * ndl;
	float wrap = saturate( ( ndlRaw + 0.5 ) / 1.5 );
	float backLight = saturate( -dot( s.V, L ) );
	float3 subsurface = s.SubsurfaceColor * ( ( wrap - ndl ) + 0.25 * backLight * backLight * backLight * backLight );
	direct += ( diffuse + subsurface + specular ) * radiance;

	float coatFresnel = ( 0.04 + 0.96 * SubstratePow5( 1.0 - vdh ) ) * s.Coat;
	coatDirect += radiance * ( SubstrateSpecularLobe( ndh, ndl, s.NdotV, s.CoatAlpha ) * ndl * coatFresnel );
}

float3 SubstrateShadeSlab( SubstrateSlab slab, SubstrateGeometry geometry, SubstrateLighting lighting )
{
	SubstrateSurface s;
	float3 geometryNormal = SubstrateSafeNormalize( geometry.Normal, float3( 0.0, 0.0, -1.0 ) );
	s.N = SubstrateSafeNormalize( slab.Normal, geometryNormal );
	s.V = SubstrateSafeNormalize( geometry.ToCamera, s.N );
	s.T = SubstrateMakeTangent( s.N, geometry.Tangent );
	s.B = cross( s.N, s.T );
	s.NdotV = max( dot( s.N, s.V ), 1.0e-4 );

	float roughness = saturate( slab.Roughness );
	float metallic = saturate( slab.Metallic );

	s.SpecularColor = lerp( slab.F0, slab.BaseColor, metallic );
	float film = saturate( slab.ThinFilm );
	float phase = 2.0 * 3.14159265 * ( 1.0 + 2.0 * film ) * s.NdotV;
	float3 filmColor = 0.5 + 0.5 * cos( phase + float3( 0.0, 2.0943951, 4.1887902 ) );
	s.SpecularColor = lerp( s.SpecularColor, filmColor, film );

	float alpha = max( roughness * roughness, 0.002 );
	float anisotropy = clamp( slab.Anisotropy, -1.0, 1.0 );
	float aspect = sqrt( 1.0 - 0.9 * abs( anisotropy ) );
	s.AlphaT = min( anisotropy >= 0.0 ? alpha / aspect : alpha * aspect, 1.0 );
	s.AlphaB = min( anisotropy >= 0.0 ? alpha * aspect : alpha / aspect, 1.0 );

	s.DiffuseColor = slab.BaseColor * ( 1.0 - metallic ) * slab.DiffuseScale;
	s.SubsurfaceColor = max( slab.Subsurface, 0.0 ) * slab.DiffuseScale;
	s.Coat = saturate( slab.ClearCoat );
	float coatRoughness = saturate( slab.ClearCoatRoughness );
	s.CoatAlpha = max( coatRoughness * coatRoughness, 0.002 );

	float3 direct = ( float3 )0.0;
	float3 coatDirect = ( float3 )0.0;
	SubstrateAddLight( s, SubstrateSafeNormalize( geometry.ToLight, s.N ), lighting.LightColor * saturate( lighting.Shadow ), direct, coatDirect );
	int numPointLights = clamp( lighting.NumPointLights, 0, 4 );
	for ( int index = 0; index < numPointLights; ++index )
	{
		float3 offset = lighting.PointLightPositions[index].xyz - geometry.Position;
		float radius = lighting.PointLightPositions[index].w;
		float falloff = saturate( 1.0 - dot( offset, offset ) / max( radius * radius, 1.0e-6 ) );
		SubstrateAddLight( s, SubstrateSafeNormalize( offset, s.N ), lighting.PointLightColors[index].rgb * ( falloff * falloff ), direct, coatDirect );
	}

	float3 reflected = s.N * ( 2.0 * dot( s.N, s.V ) ) - s.V;
	float3 environmentFresnel = s.SpecularColor + ( 1.0 - s.SpecularColor ) * ( SubstratePow5( 1.0 - s.NdotV ) * ( 1.0 - roughness ) );
	float3 ambient = s.DiffuseColor * SubstrateEnvironment( s.N, 1.0, lighting ) + environmentFresnel * SubstrateEnvironment( reflected, roughness, lighting );
	float3 color = direct + ambient;

	float coatFresnelView = ( 0.04 + 0.96 * SubstratePow5( 1.0 - s.NdotV ) ) * s.Coat;
	color = color * ( 1.0 - coatFresnelView ) + coatDirect + SubstrateEnvironment( reflected, coatRoughness, lighting ) * coatFresnelView;

	return ( color + slab.Emissive ) * slab.Throughput;
}
