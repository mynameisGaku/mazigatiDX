// SPDX-License-Identifier: MIT

cbuffer cbObject : register( b0 )
{
	row_major float4x4 g_Clip;
	row_major float4x4 g_World;
	row_major float4x4 g_NormalMatrix;
	float4 g_Params; // x = 光の計算、y = 不透明度、z = 画素照明、w = 画像あり
	float4 g_UvScaleCenter;
	float4 g_UvTranslationRotation;
	float4 g_FogDepth;
	float4 g_DistanceFog;
	float4 g_DistanceFogColor;
	float4 g_HeightFog;
	float4 g_HeightFogColor;
};
cbuffer cbLight : register( b6 )
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
Texture2D g_Base : register( t0 );
SamplerState g_Sampler : register( s0 );
struct VI { float3 Position : POSITION; float3 Normal : NORMAL; float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; };
struct VO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; float3 World : TEXCOORD1; float3 Normal : TEXCOORD2; float3 Specular : COLOR1; float4 Fog : TEXCOORD3; };
float3 ForwardLight( float3 world, float3 n, float3 vertexDiffuse, float3 vertexSpecular )
{
	float3 toEye = normalize( g_LightEye.xyz - world );
	float3 diffuseColor = g_LightMaterialDiffuse.a > 0.5 ? vertexDiffuse : g_LightMaterialDiffuse.rgb;
	float3 specularColor = g_LightMaterialSpecular.a > 0.5 ? vertexSpecular : g_LightMaterialSpecular.rgb;
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
			float3 offset = world - position.xyz;
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
float FogVisibility( float4 settings, float value, bool height )
{
	int mode = int( settings.w );
	float depth = height ? max( 0.0, settings.x - value ) : max( 0.0, value );
	float visibility = 1.0;
	if ( mode == 1 ) { visibility = exp( -settings.z * depth ); }
	else if ( mode == 2 ) { float d = settings.z * depth; visibility = exp( -d * d ); }
	else if ( mode == 3 )
	{
		if ( height ) { visibility = 1.0 - saturate( settings.y != settings.x ? ( value - settings.x ) / ( settings.y - settings.x ) : ( value < settings.y ? 1.0 : 0.0 ) ); }
		else { visibility = settings.y > settings.x ? ( settings.y - depth ) / ( settings.y - settings.x ) : ( depth < settings.y ? 1.0 : 0.0 ); }
	}
	return isfinite( visibility ) ? saturate( visibility ) : 1.0;
}
float3 RoundFogColor( float3 value ) { return floor( saturate( value ) * 255.0 + 0.5 ) / 255.0; }
VO ForwardVS( VI i )
{
	VO o;
	float4 local = float4( i.Position, 1.0 );
	o.Position = mul( local, g_Clip );
	o.World = mul( local, g_World ).xyz;
	float3 normal = mul( float4( i.Normal, 0.0 ), g_NormalMatrix ).xyz;
	float normalLength = length( normal );
	o.Normal = normalLength > 1.0e-4 ? normal / normalLength : float3( 0.0, 1.0, 0.0 );
	o.Specular = i.Specular.rgb;
	float3 color;
	if ( g_Params.z > 0.5 ) { color = i.Diffuse.rgb; }
	else if ( g_Params.x > 0.5 )
	{
		color = ForwardLight( o.World, o.Normal, i.Diffuse.rgb, i.Specular.rgb );
	}
	else
	{
		color = saturate( i.Diffuse.rgb + i.Specular.rgb ) * g_LightBright.rgb;
	}
	o.Fog = float4( 0.0, 0.0, 0.0, 1.0 );
	if ( g_DistanceFog.w != 0.0 || g_HeightFog.w != 0.0 )
	{
		float distanceVisibility = FogVisibility( g_DistanceFog, dot( float4( o.World, 1.0 ), g_FogDepth ), false );
		float heightVisibility = FogVisibility( g_HeightFog, o.World.y, true );
		color = RoundFogColor( color );
		if ( g_Params.w > 0.5 )
		{
			float visibility = distanceVisibility * heightVisibility;
			float weight = 1.0 - visibility;
			float3 fogColor = weight > 1.0e-6 ? ( heightVisibility * ( 1.0 - distanceVisibility ) * g_DistanceFogColor.rgb + ( 1.0 - heightVisibility ) * g_HeightFogColor.rgb ) / weight : float3( 0.0, 0.0, 0.0 );
			o.Fog = float4( RoundFogColor( fogColor ), floor( saturate( visibility ) * 255.0 + 0.5 ) / 255.0 );
		}
		else
		{
			if ( g_DistanceFog.w != 0.0 ) { color = RoundFogColor( lerp( g_DistanceFogColor.rgb, color, distanceVisibility ) ); }
			if ( g_HeightFog.w != 0.0 ) { color = RoundFogColor( lerp( g_HeightFogColor.rgb, color, heightVisibility ) ); }
		}
	}
	o.Color = float4( color, i.Diffuse.a * g_Params.y );
	float2 centered = ( i.Uv - g_UvScaleCenter.zw ) * g_UvScaleCenter.xy;
	o.Uv = float2( centered.x * g_UvTranslationRotation.w - centered.y * g_UvTranslationRotation.z, centered.x * g_UvTranslationRotation.z + centered.y * g_UvTranslationRotation.w ) + g_UvScaleCenter.zw + g_UvTranslationRotation.xy;
	return o;
}
float4 ForwardColor( VO i )
{
	return g_Params.z > 0.5 ? float4( ForwardLight( i.World, normalize( i.Normal ), i.Color.rgb, i.Specular ), i.Color.a ) : i.Color;
}
float4 ForwardPS( VO i ) : SV_Target { return ForwardColor( i ); }
float4 ForwardTexturedPS( VO i ) : SV_Target
{
	float4 c = ForwardColor( i ) * g_Base.Sample( g_Sampler, i.Uv );
	if ( c.a <= 0.0 ) { discard; }
	c.rgb = lerp( i.Fog.rgb, c.rgb, i.Fog.a );
	return c;
}
