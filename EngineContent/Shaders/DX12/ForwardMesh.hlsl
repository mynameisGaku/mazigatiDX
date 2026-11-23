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
	row_major float4x4 g_LightViewProjection;
	float4 g_ShadowParams; // x = 影の絵の幅、y = 高さ、z = 補正の深さ、w = 影を受けるスロットの印（0 なら、影を受けない）
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
#ifdef SKINNED
// GPU スキニング。骨の行列（3 つの float4 = 行列の 1〜3 列。v * M の行ベクトルの規約）。MaxStaticModelSkinBones（256）個ぶん。
cbuffer cbBones : register( b1 )
{
	float4 g_Bones[3 * 256];
};
// 頂点ごとの骨と重み（FStaticModelSkinVertex と同じ 24 バイト。骨の番号は 16 ビットを 2 つずつ）。重みの合計は 1。
struct SkinVertex { uint Bones01; uint Bones23; float4 Weights; };
StructuredBuffer<SkinVertex> g_Skin : register( t2 );
// 骨の変形の前の位置と法線を、骨と重みで変形する（NModelRig::Deform と同じ式）。
void SkinVertexData( uint id, inout float3 position, inout float3 normal )
{
	SkinVertex skin = g_Skin[id];
	uint4 bones = uint4( skin.Bones01 & 0xFFFFu, skin.Bones01 >> 16, skin.Bones23 & 0xFFFFu, skin.Bones23 >> 16 );
	float4 p = float4( position, 1.0 );
	float3 skinnedPosition = float3( 0.0, 0.0, 0.0 );
	float3 skinnedNormal = float3( 0.0, 0.0, 0.0 );
	[unroll] for ( int k = 0; k < 4; ++k )
	{
		float weight = skin.Weights[k];
		uint b = bones[k] * 3;
		float4 c0 = g_Bones[b];
		float4 c1 = g_Bones[b + 1];
		float4 c2 = g_Bones[b + 2];
		skinnedPosition += float3( dot( p, c0 ), dot( p, c1 ), dot( p, c2 ) ) * weight;
		skinnedNormal += float3( dot( normal, c0.xyz ), dot( normal, c1.xyz ), dot( normal, c2.xyz ) ) * weight;
	}
	position = skinnedPosition;
	normal = skinnedNormal;
}
#endif
Texture2D g_Base : register( t0 );
Texture2D g_ShadowMap : register( t1 );
SamplerState g_Sampler : register( s0 );
struct VI { float3 Position : POSITION; float3 Normal : NORMAL; float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; };
struct VO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; float3 World : TEXCOORD1; float3 Normal : TEXCOORD2; float3 Specular : COLOR1; float4 Fog : TEXCOORD3; float3 Shadowed : TEXCOORD4; float3 LightPos : TEXCOORD5; };
// 影を受けるライト（印が g_ShadowParams.w と重なる）の分は、shadowed へ。それ以外（と、環境光・自己発光）は、戻り値へ。
float3 ForwardLightSplit( float3 world, float3 n, float3 vertexDiffuse, float3 vertexSpecular, out float3 shadowed )
{
	float3 toEye = normalize( g_LightEye.xyz - world );
	float3 diffuseColor = g_LightMaterialDiffuse.a > 0.5 ? vertexDiffuse : g_LightMaterialDiffuse.rgb;
	float3 specularColor = g_LightMaterialSpecular.a > 0.5 ? vertexSpecular : g_LightMaterialSpecular.rgb;
	float3 materialAmbient = g_LightMaterialAmbient.rgb;
	float3 sum = g_LightBase.rgb;
	float3 shadowedSum = float3( 0.0, 0.0, 0.0 );
	int shadowMask = int( g_ShadowParams.w );
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
		float3 contribution = reach * ( diffuseColor * lightDiffuse * angle + materialAmbient * lightAmbient + specularColor * lightSpecular * highlight );
		if ( ( int( spot.y ) & shadowMask ) != 0 ) { shadowedSum += contribution; }
		else { sum += contribution; }
	}
	shadowed = saturate( shadowedSum ) * g_LightBright.rgb;
	return saturate( sum ) * g_LightBright.rgb;
}
float3 ForwardLight( float3 world, float3 n, float3 vertexDiffuse, float3 vertexSpecular )
{
	float3 shadowed;
	return ForwardLightSplit( world, n, vertexDiffuse, vertexSpecular, shadowed );
}
// 影の絵と比べて、光が届く割合（3x3 の平均）。BuiltIn.hlsl の ShadowFactor と同じ。
float ShadowFactor( float3 p )
{
	if ( p.z < 0.0 || p.z > 1.0 || abs( p.x ) > 1.0 || abs( p.y ) > 1.0 ) { return 1.0; }
	float2 size = g_ShadowParams.xy;
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
			lit += ( p.z - g_ShadowParams.z <= stored ) ? 1.0 : 0.0;
		}
	}
	return lit / 9.0;
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
#ifdef SKINNED
VO ForwardVS( VI i, uint vertexId : SV_VertexID )
#else
VO ForwardVS( VI i )
#endif
{
	VO o;
	float3 modelPosition = i.Position;
	float3 modelNormal = i.Normal;
#ifdef SKINNED
	SkinVertexData( vertexId, modelPosition, modelNormal );
#endif
	float4 local = float4( modelPosition, 1.0 );
	o.Position = mul( local, g_Clip );
	o.World = mul( local, g_World ).xyz;
	float3 normal = mul( float4( modelNormal, 0.0 ), g_NormalMatrix ).xyz;
	float normalLength = length( normal );
	o.Normal = normalLength > 1.0e-4 ? normal / normalLength : float3( 0.0, 1.0, 0.0 );
	o.Specular = i.Specular.rgb;
	float3 color;
	float3 shadowed = float3( 0.0, 0.0, 0.0 );
	if ( g_Params.z > 0.5 ) { color = i.Diffuse.rgb; }
	else if ( g_Params.x > 0.5 )
	{
		color = ForwardLightSplit( o.World, o.Normal, i.Diffuse.rgb, i.Specular.rgb, shadowed );
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
	o.Shadowed = shadowed;
	o.LightPos = g_ShadowParams.w > 0.5 ? mul( float4( o.World, 1.0 ), g_LightViewProjection ).xyz : float3( 0.0, 0.0, 0.0 );
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
float4 ForwardShadowColor( VO i ) { float s = ShadowFactor( i.LightPos ); return float4( i.Color.rgb + i.Shadowed * s, i.Color.a ); }
float4 ForwardShadowPS( VO i ) : SV_Target { float4 c = ForwardShadowColor( i ); if ( c.a <= 0.0 ) { discard; } return c; }
float4 ForwardShadowTexturedPS( VO i ) : SV_Target { float4 c = ForwardShadowColor( i ) * g_Base.Sample( g_Sampler, i.Uv ); if ( c.a <= 0.0 ) { discard; } return c; }
// 影の絵へ、光から見た深度だけを書く（BuiltIn.hlsl の ShadowDepthMain と同じ）。b0 の g_Clip だけを使う。
struct DepthVO { float4 Position : SV_POSITION; };
#ifdef SKINNED
DepthVO ForwardDepthVS( VI i, uint vertexId : SV_VertexID )
{
	float3 modelPosition = i.Position;
	float3 modelNormal = i.Normal;
	SkinVertexData( vertexId, modelPosition, modelNormal );
	DepthVO o;
	o.Position = mul( float4( modelPosition, 1.0 ), g_Clip );
	return o;
}
#else
DepthVO ForwardDepthVS( VI i ) { DepthVO o; o.Position = mul( float4( i.Position, 1.0 ), g_Clip ); return o; }
#endif
float4 ForwardDepthPS( DepthVO i ) : SV_Target { return float4( i.Position.z, 0.0, 0.0, 1.0 ); }
