// SPDX-License-Identifier: MIT
struct VI { float3 Position : POSITION; float4 SubPosition : POSITION1; float3 Normal : NORMAL; float3 Tangent : TANGENT; float3 Binormal : BINORMAL;
            float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; float2 SubUv : TEXCOORD1; };
struct VO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; float4 Extra : TEXCOORD1; };
VO RectMain( VI i ) { VO o; o.Position = float4( i.Position, 1 ); o.Color = i.Diffuse; o.Uv = i.Uv; o.Extra = float4( i.SubUv, i.Tangent.xy ); return o; }
VO QuadMain( VI i ) { VO o; o.Position = i.SubPosition; o.Color = i.Diffuse; o.Uv = i.Uv; o.Extra = float4( i.SubUv, i.Tangent.xy ); return o; }
float4 ShadowDepthMain( VO i ) : SV_Target { return float4( i.Position.z, 0, 0, 1 ); }
struct LitOutput { float4 Position : SV_POSITION; float4 Color : COLOR0; float4 Shadowed : COLOR1; float2 Uv : TEXCOORD0; float4 LightPosition : TEXCOORD1; };
LitOutput LitMain( VI i ) { LitOutput o; o.Position = i.SubPosition; o.Color = i.Diffuse; o.Shadowed = i.Specular; o.Uv = i.Uv; o.LightPosition = float4( i.Normal, 1 ); return o; }
cbuffer cbShadow : register( b4 ) { float4 g_Shadow; };
Texture2D g_Base : register( t0 );
Texture2D g_ShadowMap : register( t1 );
SamplerState g_Sampler : register( s0 );
float ShadowFactor( float3 p )
{
	if ( p.z < 0.0 || p.z > 1.0 || abs( p.x ) > 1.0 || abs( p.y ) > 1.0 ) { return 1.0; }
	float2 size = g_Shadow.xy;
	float2 texel = float2( p.x * 0.5 + 0.5, 0.5 - p.y * 0.5 ) * size;
	int2 center = int2( floor( texel ) );
	int2 last = int2( size ) - 1;
	float lit = 0.0;
	for ( int y = -1; y <= 1; ++y )
	{
		for ( int x = -1; x <= 1; ++x )
		{
			int2 t = clamp( center + int2( x, y ), int2( 0, 0 ), last );
			float stored = g_ShadowMap.Load( int3( t, 0 ) ).r;
			lit += ( p.z - g_Shadow.z <= stored ) ? 1.0 : 0.0;
		}
	}
	return lit / 9.0;
}
float4 LitColor( LitOutput i, float4 baseColor ) { float s = ShadowFactor( i.LightPosition.xyz ); return float4( i.Color.rgb + i.Shadowed.rgb * s, i.Color.a ) * baseColor; }
float4 ShadowLitMain( LitOutput i ) : SV_Target { float4 c = LitColor( i, float4( 1, 1, 1, 1 ) ); if ( c.a <= 0.0 ) { discard; } return c; }
float4 ShadowLitTexturedMain( LitOutput i ) : SV_Target { float4 c = LitColor( i, g_Base.Sample( g_Sampler, i.Uv ) ); if ( c.a <= 0.0 ) { discard; } return c; }
float4 FogTexturedMain( LitOutput i ) : SV_Target { float4 c = g_Base.Sample( g_Sampler, i.Uv ) * i.Color; if ( c.a <= 0.0 ) { discard; } c.rgb = lerp( i.Shadowed.rgb, c.rgb, i.Shadowed.a ); return c; }
cbuffer cbImageOp : register( b5 ) { int4 g_Op; int4 g_Where; int4 g_ArgsA; int4 g_ArgsB; };
Texture2D g_Aux : register( t2 );
float4 FetchBase( int2 p ) { uint w, h; g_Base.GetDimensions( w, h ); return g_Base.Load( int3( clamp( p, int2( 0, 0 ), int2( w, h ) - 1 ), 0 ) ); }
float4 FetchAux( int2 p ) { uint w, h; g_Aux.GetDimensions( w, h ); return g_Aux.Load( int3( clamp( p, int2( 0, 0 ), int2( w, h ) - 1 ), 0 ) ); }
float Luma( float3 c ) { return dot( c, float3( 0.299, 0.587, 0.114 ) ); }
float4 Unpremultiply( float4 c ) { if ( c.a > 0.0 ) { c.rgb /= c.a; } return c; }
float3 UnpackRgb( int v ) { return float3( ( v >> 16 ) & 255, ( v >> 8 ) & 255, v & 255 ) / 255.0; }
float3 RgbToHsv( float3 c )
{
	float4 k = float4( 0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0 );
	float4 p = lerp( float4( c.bg, k.wz ), float4( c.gb, k.xy ), step( c.b, c.g ) );
	float4 q = lerp( float4( p.xyw, c.r ), float4( c.r, p.yzx ), step( p.x, c.r ) );
	float d = q.x - min( q.w, q.y );
	return float3( abs( q.z + ( q.w - q.y ) / ( 6.0 * d + 1.0e-10 ) ), d / ( q.x + 1.0e-10 ), q.x );
}
float3 HsvToRgb( float3 c )
{
	float4 k = float4( 1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0 );
	float3 p = abs( frac( c.xxx + k.xyz ) * 6.0 - k.www );
	return c.z * lerp( k.xxx, saturate( p - k.xxx ), c.y );
}
float CubicWeight( float x ) { x = abs( x ); return x < 1.0 ? ( 1.5 * x - 2.5 ) * x * x + 1.0 : ( x < 2.0 ? ( ( -0.5 * x + 2.5 ) * x - 4.0 ) * x + 2.0 : 0.0 ); }
float LanczosWeight( float x ) { x = abs( x ); if ( x < 1.0e-5 ) { return 1.0; } if ( x >= 3.0 ) { return 0.0; } float px = 3.14159265 * x; return 3.0 * sin( px ) * sin( px / 3.0 ) / ( px * px ); }
float4 ScaleSample( int2 pixel, bool lanczos )
{
	uint sw, sh;
	g_Base.GetDimensions( sw, sh );
	float2 position = ( float2( pixel ) + 0.5 ) * float2( sw, sh ) / float2( g_Op.zw ) - 0.5;
	int2 origin = int2( floor( position ) );
	float2 f = position - float2( origin );
	int reach = lanczos ? 3 : 2;
	float4 sum = 0;
	float total = 0;
	[loop] for ( int y = 1 - reach; y <= reach; ++y )
	{
		[loop] for ( int x = 1 - reach; x <= reach; ++x )
		{
			float w = lanczos ? LanczosWeight( x - f.x ) * LanczosWeight( y - f.y ) : CubicWeight( x - f.x ) * CubicWeight( y - f.y );
			sum += w * FetchBase( origin + int2( x, y ) );
			total += w;
		}
	}
	return saturate( sum / total );
}
float GaussWeight( int d, float sigma ) { if ( d == 0 ) { return 2.0; } float p = 2.0 * ( ( abs( d ) + 1 ) / 2 ) - 0.5; return exp( -p * p / ( 2.0 * sigma * sigma ) ); }
float4 ImageFilterMain( VO i ) : SV_Target
{
	int type = g_Op.x;
	bool premultiplied = g_Op.y != 0 && type != 11 && type != 12;
	int2 pixel = int2( i.Position.xy ) - g_Where.zw;
	int2 source = pixel + g_Where.xy;
	float4 c = FetchBase( source );
	if ( premultiplied ) { c = Unpremultiply( c ); }
	float4 o = c;
	if ( type == 0 )
	{
		float y = Luma( c.rgb );
		float cb = g_ArgsA.x / 255.0 * 0.5;
		float cr = g_ArgsA.y / 255.0 * 0.5;
		o.rgb = saturate( float3( y + 1.402 * cr, y - 0.344136 * cb - 0.714136 * cr, y + 1.772 * cb ) );
	}
	else if ( type == 1 )
	{
		float sigma = max( 0.1436 * pow( max( g_ArgsA.y, 1 ), 0.544 ), 0.2 );
		int reach = g_ArgsA.x / 2;
		float4 sum = 0;
		float total = 0;
		[loop] for ( int y = -reach; y <= reach; ++y )
		{
			[loop] for ( int x = -reach; x <= reach; ++x )
			{
				float w = GaussWeight( x, sigma ) * GaussWeight( y, sigma );
				sum += w * FetchBase( source + int2( x, y ) );
				total += w;
			}
		}
		o = sum / total;
	}
	else if ( type == 2 )
	{
		int n = max( g_ArgsA.x, 1 );
		float4 sum = 0;
		[loop] for ( int y = 0; y < n; ++y )
		{
			[loop] for ( int x = 0; x < n; ++x ) { sum += FetchBase( g_Where.xy + pixel * n + int2( x, y ) ); }
		}
		o = sum / ( n * n );
	}
	else if ( type == 3 )
	{
		float luminance = Luma( c.rgb ) * 255.0;
		bool clip = g_ArgsA.x != 0 ? luminance < g_ArgsA.y : luminance > g_ArgsA.y;
		if ( clip )
		{
			if ( g_ArgsA.z == 0 ) { discard; }
			o = float4( UnpackRgb( g_ArgsA.w ), g_ArgsB.x / 255.0 );
		}
	}
	else if ( type == 4 )
	{
		float low = g_ArgsA.x / 255.0;
		float high = g_ArgsA.y / 255.0;
		if ( low <= high ) { o.rgb = saturate( ( c.rgb - low ) / max( high - low, 1.0 / 255.0 ) ); }
		else { o.rgb = 1.0 - saturate( ( c.rgb - high ) / max( low - high, 1.0 / 255.0 ) ); }
	}
	else if ( type == 5 )
	{
		float3 hsv = RgbToHsv( c.rgb );
		hsv.x = frac( ( g_ArgsA.x == 0 ? hsv.x * 360.0 + g_ArgsA.y : g_ArgsA.y ) / 360.0 );
		hsv.y = saturate( hsv.y * ( 1.0 + g_ArgsA.z / 255.0 ) );
		o.rgb = saturate( HsvToRgb( hsv ) + g_ArgsA.w / 255.0 );
	}
	else if ( type == 6 )
	{
		o.rgb = 1.0 - c.rgb;
	}
	else if ( type == 7 )
	{
		float3 t = saturate( ( c.rgb - g_ArgsA.x / 255.0 ) / max( ( g_ArgsA.y - g_ArgsA.x ) / 255.0, 1.0 / 255.0 ) );
		t = pow( t, 100.0 / max( g_ArgsA.z, 1 ) );
		o.rgb = g_ArgsA.w / 255.0 + t * ( g_ArgsB.x - g_ArgsA.w ) / 255.0;
	}
	else if ( type == 8 )
	{
		bool low = Luma( c.rgb ) * 255.0 < g_ArgsA.x;
		o = low ? float4( UnpackRgb( g_ArgsA.y ), g_ArgsA.z / 255.0 ) : float4( UnpackRgb( g_ArgsA.w ), g_ArgsB.x / 255.0 );
	}
	else if ( type == 9 )
	{
		uint mw, mh;
		g_Aux.GetDimensions( mw, mh );
		float position = min( floor( saturate( Luma( c.rgb ) ) * mw ), mw - 1 );
		if ( g_ArgsA.x != 0 ) { position = ( mw - 1 ) - position; }
		int index = int( floor( position ) );
		float4 map = FetchAux( int2( index, 0 ) );
		o = float4( map.rgb, c.a );
	}
	else if ( type == 10 )
	{
		int4 now = int4( round( c * 255.0 ) );
		if ( all( now == int4( g_ArgsA.x, g_ArgsA.y, g_ArgsA.z, g_ArgsA.w ) ) ) { o = float4( g_ArgsB ) / 255.0; }
	}
	else if ( type == 11 )
	{
		o.rgb = c.rgb * c.a;
	}
	else if ( type == 12 )
	{
		o = Unpremultiply( c );
	}
	else if ( type == 13 || type == 14 )
	{
		o = ScaleSample( pixel, type == 14 );
	}
	else if ( type == 15 )
	{
		o = saturate( ( c - asfloat( g_ArgsB ) ) * asfloat( g_ArgsA ) );
	}
	if ( premultiplied ) { o.rgb *= o.a; }
	return o;
}
float3 BlendColor( int type, float3 b, float3 l )
{
	if ( type == 2 ) { return b * l; }
	if ( type == 3 ) { return saturate( b - l ); }
	if ( type == 4 ) { return saturate( b + l ); }
	if ( type == 5 ) { return 1.0 - ( 1.0 - b ) * ( 1.0 - l ); }
	if ( type == 6 ) { return b < 0.5 ? 2.0 * b * l : 1.0 - 2.0 * ( 1.0 - b ) * ( 1.0 - l ); }
	if ( type == 7 ) { return saturate( b / max( 1.0 - l, 1.0 / 255.0 ) ); }
	if ( type == 8 ) { return 1.0 - saturate( ( 1.0 - b ) / max( l, 1.0 / 255.0 ) ); }
	if ( type == 9 ) { return min( b, l ); }
	if ( type == 10 ) { return max( b, l ); }
	if ( type == 11 ) { return ( 1.0 - 2.0 * l ) * b * b + 2.0 * l * b; }
	if ( type == 12 ) { return l < 0.5 ? 2.0 * b * l : 1.0 - 2.0 * ( 1.0 - b ) * ( 1.0 - l ); }
	if ( type == 13 ) { return b + l - 2.0 * b * l; }
	return b;
}
float SelectComponent( int k, float4 b, float4 l )
{
	if ( k < 4 ) { return b[k]; }
	if ( k < 8 ) { return l[k - 4]; }
	if ( k < 12 ) { return 1.0 - b[k - 8]; }
	return 1.0 - l[k - 12];
}
float4 ImageBlendMain( VO i ) : SV_Target
{
	int type = g_Op.x;
	float ratio = g_Op.z / 255.0;
	int2 pixel = int2( i.Position.xy ) - g_Where.zw;
	float4 b = FetchBase( pixel );
	float4 l = FetchAux( pixel );
	if ( g_Op.y != 0 ) { b = Unpremultiply( b ); l = Unpremultiply( l ); }
	float4 o = b;
	if ( type == 0 || type == 17 )
	{
		o.rgb = lerp( b.rgb, l.rgb, l.a * ratio );
	}
	else if ( type == 1 )
	{
		float4 picked = float4( SelectComponent( g_ArgsA.x, b, l ), SelectComponent( g_ArgsA.y, b, l ), SelectComponent( g_ArgsA.z, b, l ), SelectComponent( g_ArgsA.w, b, l ) );
		o = lerp( b, picked, ratio );
	}
	else if ( type >= 2 && type <= 13 )
	{
		o.rgb = lerp( b.rgb, BlendColor( type, b.rgb, l.rgb ), ratio );
	}
	else if ( type == 14 )
	{
		float la = l.a * ratio;
		o.a = la + b.a * ( 1.0 - la );
		o.rgb = ( l.rgb * la + b.rgb * b.a * ( 1.0 - la ) ) / max( o.a, 1.0e-5 );
	}
	else if ( type == 15 )
	{
		o.rgb = saturate( b.rgb + l.rgb * l.a * ratio );
		o.a = saturate( b.a + l.a * ratio );
	}
	else if ( type == 16 )
	{
		o.a = b.a * lerp( 1.0, l.a, ratio );
	}
	if ( g_Op.y != 0 ) { o.rgb *= o.a; }
	return o;
}
struct PLO { float4 Position : SV_POSITION; float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; float3 World : TEXCOORD1; float3 Normal : TEXCOORD2; };
PLO PixelLitVS( VI i ) { PLO o; o.Position = i.SubPosition; o.Diffuse = i.Diffuse; o.Specular = i.Specular; o.Uv = i.Uv; o.World = i.Position; o.Normal = i.Normal; return o; }
cbuffer cbPixelLight : register( b6 )
{
	float4 g_LightEye;
	float4 g_LightBase;
	float4 g_LightMaterialDiffuse;
	float4 g_LightMaterialSpecular;
	float4 g_LightMaterialAmbient;
	float4 g_LightOptions;
	float4 g_LightBright;
	float4 g_Lights[42];
};
float3 PixelLightColor( PLO i )
{
	float3 n = normalize( i.Normal );
	float3 toEye = normalize( g_LightEye.xyz - i.World );
	float3 diffuseColor = g_LightMaterialDiffuse.a > 0.5 ? i.Diffuse.rgb : g_LightMaterialDiffuse.rgb;
	float3 specularColor = g_LightMaterialSpecular.a > 0.5 ? i.Specular.rgb : g_LightMaterialSpecular.rgb;
	float3 materialAmbient = g_LightMaterialAmbient.rgb;
	float3 sum = g_LightBase.rgb;
	int count = int( g_LightEye.w );
	[loop] for ( int k = 0; k < count; ++k )
	{
		float4 position = g_Lights[k * 7];
		float4 direction = g_Lights[k * 7 + 1];
		float4 attenuation = g_Lights[k * 7 + 2];
		float4 spot = g_Lights[k * 7 + 3];
		float3 lightDiffuse = g_Lights[k * 7 + 4].rgb;
		float3 lightSpecular = g_Lights[k * 7 + 5].rgb;
		float3 lightAmbient = g_Lights[k * 7 + 6].rgb;
		int type = int( position.w );
		float3 toLight = normalize( -direction.xyz );
		float reach = 1.0;
		if ( type != 0 )
		{
			float3 offset = i.World - position.xyz;
			float dist = length( offset );
			toLight = normalize( -offset );
			if ( dist > direction.w )
			{
				reach = 0.0;
			}
			else
			{
				reach = 1.0 / ( attenuation.x + attenuation.y * dist + attenuation.z * dist * dist );
				if ( type == 2 )
				{
					float cosAngle = dot( direction.xyz, offset ) / max( dist, 1.0e-6 );
					reach *= cosAngle <= attenuation.w ? 0.0 : ( cosAngle >= spot.x ? 1.0 : ( cosAngle - attenuation.w ) / ( spot.x - attenuation.w ) );
				}
			}
		}
		if ( reach == 0.0 ) { continue; }
		float d = dot( n, toLight );
		float angle = max( d, 0.0 );
		if ( g_LightOptions.z > 0.5 ) { angle = 1.0; }
		else if ( g_LightOptions.y > 0.5 ) { float w = d * 0.5 + 0.5; angle = w * w; }
		float highlight = 0.0;
		if ( g_LightOptions.x > 0.5 && d > 0.0 )
		{
			float b = dot( n, normalize( toLight + toEye ) );
			if ( b > 0.0 ) { highlight = pow( b, g_LightMaterialAmbient.a ); }
		}
		sum += reach * ( diffuseColor * lightDiffuse * angle + materialAmbient * lightAmbient + specularColor * lightSpecular * highlight );
	}
	return saturate( sum ) * g_LightBright.rgb;
}
float4 PixelLitMain( PLO i ) : SV_Target { return float4( PixelLightColor( i ), i.Diffuse.a ); }
float4 ShadowPreviewMain( VO i ) : SV_Target { uint w, h; g_ShadowMap.GetDimensions( w, h ); int2 p = clamp( int2( i.Uv * float2( w, h ) ), int2( 0, 0 ), int2( w, h ) - 1 ); return float4( saturate( g_ShadowMap.Load( int3( p, 0 ) ).r ), 0, 0, 1 ); }
float4 PixelLitTexturedMain( PLO i ) : SV_Target { float4 c = float4( PixelLightColor( i ), i.Diffuse.a ) * g_Base.Sample( g_Sampler, i.Uv ); if ( c.a <= 0.0 ) { discard; } return c; }
struct MEO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; float3 Params : TEXCOORD1; float4 Offsets : TEXCOORD2; };
MEO EffectMaterialVS( VI i ) { MEO o; o.Position = i.SubPosition; o.Color = i.Diffuse; o.Uv = i.Uv; o.Params = i.Normal; o.Offsets = float4( i.Tangent.xy, i.Binormal.xy ); return o; }
cbuffer cbEffectMaterial : register( b7 ) { float4 g_EffectA; float4 g_EffectB; float4 g_EffectEdge; float4 g_EffectBright; float4 g_EffectDistortion; };
float EffHash( float2 p ) { return frac( sin( dot( p, float2( 127.1, 311.7 ) ) ) * 43758.5453 ); }
float EffNoise( float2 p ) { float2 i = floor( p ); float2 f = frac( p ); f = f * f * ( 3.0 - 2.0 * f ); return lerp( lerp( EffHash( i ), EffHash( i + float2( 1, 0 ) ), f.x ), lerp( EffHash( i + float2( 0, 1 ) ), EffHash( i + 1.0 ), f.x ), f.y ); }
float4 EffectMaterialPS( MEO i ) : SV_Target
{
	uint w, h; g_Base.GetDimensions( w, h );
	float2 inset = 0.5 / float2( w, h );
	float2 uv = i.Uv * g_EffectA.xy + i.Offsets.xy;
	if ( g_EffectB.z > 0.5 )
	{
		uint dw, dh; g_Aux.GetDimensions( dw, dh );
		float2 dinset = 0.5 / float2( dw, dh );
		float2 duv = clamp( frac( i.Uv * g_EffectDistortion.xy + i.Offsets.zw ), dinset, 1.0 - dinset );
		uv += ( g_Aux.SampleLevel( g_Sampler, duv, 0 ).rg * 2.0 - 1.0 ) * i.Params.z;
	}
	uv = clamp( frac( uv ), inset, 1.0 - inset );
	float2 muv = frac( i.Uv * g_EffectA.z );
	float mask;
	if ( g_EffectA.w > 0.5 )
	{
		uint mw, mh; g_ShadowMap.GetDimensions( mw, mh );
		float2 minset = 0.5 / float2( mw, mh );
		mask = g_ShadowMap.SampleLevel( g_Sampler, clamp( muv, minset, 1.0 - minset ), 0 ).r;
	}
	else
	{
		mask = saturate( EffNoise( muv * 4.0 ) * 0.5 + EffNoise( muv * 16.0 ) * 0.35 + EffNoise( muv * 64.0 ) * 0.15 );
	}
	float dissolve = i.Params.x;
	float edge = 0.0;
	if ( g_EffectB.x > 0.5 )
	{
		if ( dissolve >= 1.0 ) { discard; }
		clip( mask - dissolve );
		if ( dissolve > 0.0 && g_EffectB.w > 0.0 ) { edge = 1.0 - smoothstep( 0.0, g_EffectB.w, mask - dissolve ); }
	}
	float4 src = g_Base.SampleLevel( g_Sampler, uv, 0 );
	float alpha = src.a * i.Color.a * g_EffectB.y;
	if ( alpha <= 0.0 ) { discard; }
	float3 emissive = src.rgb * i.Color.rgb * i.Params.y + g_EffectEdge.rgb * edge;
	return float4( min( emissive * g_EffectBright.rgb, 1.0 ), alpha );
}
