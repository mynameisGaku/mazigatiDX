// SPDX-License-Identifier: MIT
#include "DX12ShaderService.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment( lib, "d3dcompiler.lib" )

namespace
{
	// 頂点シェーダーの入力のセマンティックを、FDrawShaderVertex3D（88 バイト）の中の位置と型にする。知らない名前は false
	bool MapSemantic( const FString& Name, uint32 Index, EGraphicsVertexAttributeFormat& OutFormat, uint32& OutOffset )
	{
		struct FField
		{
			const char* Name;
			uint32 Index;
			EGraphicsVertexAttributeFormat Format;
			uint32 Offset;
		};
		static const FField Fields[] = {
			{ "POSITION", 0, EGraphicsVertexAttributeFormat::Float3, 0 },   { "POSITION", 1, EGraphicsVertexAttributeFormat::Float4, 12 },
			{ "NORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 28 },    { "TANGENT", 0, EGraphicsVertexAttributeFormat::Float3, 40 },
			{ "BINORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 52 },  { "COLOR", 0, EGraphicsVertexAttributeFormat::UByte4Norm, 64 },
			{ "COLOR", 1, EGraphicsVertexAttributeFormat::UByte4Norm, 68 }, { "TEXCOORD", 0, EGraphicsVertexAttributeFormat::Float2, 72 },
			{ "TEXCOORD", 1, EGraphicsVertexAttributeFormat::Float2, 80 },
		};
		for ( const FField& Field : Fields )
		{
			if ( Field.Index == Index && Name.Equals( Field.Name, ESearchCase::IgnoreCase ) )
			{
				OutFormat = Field.Format;
				OutOffset = Field.Offset;
				return true;
			}
		}
		return false;
	}

	// 4 つの成分のうち、その形で読める成分の印（x = 1, y = 2, z = 4, w = 8）
	uint32 GetFormatMask( EGraphicsVertexAttributeFormat Format )
	{
		switch ( Format )
		{
		case EGraphicsVertexAttributeFormat::Float1: return 1;
		case EGraphicsVertexAttributeFormat::Float2: return 3;
		case EGraphicsVertexAttributeFormat::Float3: return 7;
		default: return 15;
		}
	}

	// 組み込みの頂点シェーダー（ShaderRect / ShaderQuad3D）。入力は、FDrawShaderVertex3D の全部。出力は、ピクセルシェーダーへ、色・画像の座標・Extra
	constexpr const char* BuiltInSource =
		"struct VI { float3 Position : POSITION; float4 SubPosition : POSITION1; float3 Normal : NORMAL; float3 Tangent : TANGENT; float3 Binormal : BINORMAL;\n"
		"            float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; float2 SubUv : TEXCOORD1; };\n"
		"struct VO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; float4 Extra : TEXCOORD1; };\n"
		"VO RectMain( VI i ) { VO o; o.Position = float4( i.Position, 1 ); o.Color = i.Diffuse; o.Uv = i.Uv; o.Extra = float4( i.SubUv, i.Tangent.xy ); return o; }\n"
		"VO QuadMain( VI i ) { VO o; o.Position = i.SubPosition; o.Color = i.Diffuse; o.Uv = i.Uv; o.Extra = float4( i.SubUv, i.Tangent.xy ); return o; }\n"
		// 影: 影の絵へ、光から見た深度を書く
		"float4 ShadowDepthMain( VO i ) : SV_Target { return float4( i.Position.z, 0, 0, 1 ); }\n"
		// 影: 影を受ける光の計算（頂点の Diffuse = 影の外の色、Specular = 影の中で消える色、Normal = 光から見た位置）
		"struct LitOutput { float4 Position : SV_POSITION; float4 Color : COLOR0; float4 Shadowed : COLOR1; float2 Uv : TEXCOORD0; float4 LightPosition : TEXCOORD1; };\n"
		"LitOutput LitMain( VI i ) { LitOutput o; o.Position = i.SubPosition; o.Color = i.Diffuse; o.Shadowed = i.Specular; o.Uv = i.Uv; o.LightPosition = float4( i.Normal, 1 ); return o; }\n"
		"cbuffer cbShadow : register( b4 ) { float4 g_Shadow; };\n"
		"Texture2D g_Base : register( t0 );\n"
		"Texture2D g_ShadowMap : register( t1 );\n"
		"SamplerState g_Sampler : register( s0 );\n"
		// 3 x 3 の画素で、影の絵と比べて、光の当たる割合を返す（影を計算する範囲の外は、光が当たる）
		"float ShadowFactor( float3 p )\n"
		"{\n"
		"	if ( p.z < 0.0 || p.z > 1.0 || abs( p.x ) > 1.0 || abs( p.y ) > 1.0 ) { return 1.0; }\n"
		"	float2 size = g_Shadow.xy;\n"
		"	float2 texel = float2( p.x * 0.5 + 0.5, 0.5 - p.y * 0.5 ) * size;\n"
		"	int2 center = int2( floor( texel ) );\n"
		"	int2 last = int2( size ) - 1;\n"
		"	float lit = 0.0;\n"
		"	for ( int y = -1; y <= 1; ++y )\n"
		"	{\n"
		"		for ( int x = -1; x <= 1; ++x )\n"
		"		{\n"
		"			int2 t = clamp( center + int2( x, y ), int2( 0, 0 ), last );\n"
		"			float stored = g_ShadowMap.Load( int3( t, 0 ) ).r;\n"
		"			lit += ( p.z - g_Shadow.z <= stored ) ? 1.0 : 0.0;\n"
		"		}\n"
		"	}\n"
		"	return lit / 9.0;\n"
		"}\n"
		"float4 LitColor( LitOutput i, float4 baseColor ) { float s = ShadowFactor( i.LightPosition.xyz ); return float4( i.Color.rgb + i.Shadowed.rgb * s, i.Color.a ) * baseColor; }\n"
		"float4 ShadowLitMain( LitOutput i ) : SV_Target { float4 c = LitColor( i, float4( 1, 1, 1, 1 ) ); if ( c.a <= 0.0 ) { discard; } return c; }\n"
		"float4 ShadowLitTexturedMain( LitOutput i ) : SV_Target { float4 c = LitColor( i, g_Base.Sample( g_Sampler, i.Uv ) ); if ( c.a <= 0.0 ) { discard; } return c; }\n"
		// フォグ: 画像と頂点の色の積を、フォグの色へ混ぜる（頂点の Specular の RGB = フォグの色、A = 隠れない割合）
		"float4 FogTexturedMain( LitOutput i ) : SV_Target { float4 c = g_Base.Sample( g_Sampler, i.Uv ) * i.Color; if ( c.a <= 0.0 ) { discard; } c.rgb = lerp( i.Shadowed.rgb, c.rgb, i.Shadowed.a ); return c; }\n"
		// 画像の処理: フィルター（ImageFilter）と、2 枚の混ぜ合わせ（ImageBlend）。b5 = ( 種類, 乗算済みか, 矩形の大きさ または 混ぜる強さ ), ( 元の左上, 先の左上 ), 引数 A, 引数 B。t0 = 元の画像、t2 = 混ぜる画像（グラデーションマップ）。画素の座標で読む
		"cbuffer cbImageOp : register( b5 ) { int4 g_Op; int4 g_Where; int4 g_ArgsA; int4 g_ArgsB; };\n"
		"Texture2D g_Aux : register( t2 );\n"
		"float4 FetchBase( int2 p ) { uint w, h; g_Base.GetDimensions( w, h ); return g_Base.Load( int3( clamp( p, int2( 0, 0 ), int2( w, h ) - 1 ), 0 ) ); }\n"
		"float4 FetchAux( int2 p ) { uint w, h; g_Aux.GetDimensions( w, h ); return g_Aux.Load( int3( clamp( p, int2( 0, 0 ), int2( w, h ) - 1 ), 0 ) ); }\n"
		"float Luma( float3 c ) { return dot( c, float3( 0.299, 0.587, 0.114 ) ); }\n"
		"float4 Unpremultiply( float4 c ) { if ( c.a > 0.0 ) { c.rgb /= c.a; } return c; }\n"
		"float3 UnpackRgb( int v ) { return float3( ( v >> 16 ) & 255, ( v >> 8 ) & 255, v & 255 ) / 255.0; }\n"
		"float3 RgbToHsv( float3 c )\n"
		"{\n"
		"	float4 k = float4( 0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0 );\n"
		"	float4 p = lerp( float4( c.bg, k.wz ), float4( c.gb, k.xy ), step( c.b, c.g ) );\n"
		"	float4 q = lerp( float4( p.xyw, c.r ), float4( c.r, p.yzx ), step( p.x, c.r ) );\n"
		"	float d = q.x - min( q.w, q.y );\n"
		"	return float3( abs( q.z + ( q.w - q.y ) / ( 6.0 * d + 1.0e-10 ) ), d / ( q.x + 1.0e-10 ), q.x );\n"
		"}\n"
		"float3 HsvToRgb( float3 c )\n"
		"{\n"
		"	float4 k = float4( 1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0 );\n"
		"	float3 p = abs( frac( c.xxx + k.xyz ) * 6.0 - k.www );\n"
		"	return c.z * lerp( k.xxx, saturate( p - k.xxx ), c.y );\n"
		"}\n"
		"float CubicWeight( float x ) { x = abs( x ); return x < 1.0 ? ( 1.5 * x - 2.5 ) * x * x + 1.0 : ( x < 2.0 ? ( ( -0.5 * x + 2.5 ) * x - 4.0 ) * x + 2.0 : 0.0 ); }\n"
		"float LanczosWeight( float x ) { x = abs( x ); if ( x < 1.0e-5 ) { return 1.0; } if ( x >= 3.0 ) { return 0.0; } float px = 3.14159265 * x; return 3.0 * sin( px ) * sin( px / 3.0 ) / ( px * px ); }\n"
		"float4 ScaleSample( int2 pixel, bool lanczos )\n"
		"{\n"
		"	uint sw, sh;\n"
		"	g_Base.GetDimensions( sw, sh );\n"
		"	float2 position = ( float2( pixel ) + 0.5 ) * float2( sw, sh ) / float2( g_Op.zw ) - 0.5;\n"
		"	int2 origin = int2( floor( position ) );\n"
		"	float2 f = position - float2( origin );\n"
		"	int reach = lanczos ? 3 : 2;\n"
		"	float4 sum = 0;\n"
		"	float total = 0;\n"
		"	[loop] for ( int y = 1 - reach; y <= reach; ++y )\n"
		"	{\n"
		"		[loop] for ( int x = 1 - reach; x <= reach; ++x )\n"
		"		{\n"
		"			float w = lanczos ? LanczosWeight( x - f.x ) * LanczosWeight( y - f.y ) : CubicWeight( x - f.x ) * CubicWeight( y - f.y );\n"
		"			sum += w * FetchBase( origin + int2( x, y ) );\n"
		"			total += w;\n"
		"		}\n"
		"	}\n"
		"	return saturate( sum / total );\n"
		"}\n"
		"float GaussWeight( int d, float sigma ) { if ( d == 0 ) { return 2.0; } float p = 2.0 * ( ( abs( d ) + 1 ) / 2 ) - 0.5; return exp( -p * p / ( 2.0 * sigma * sigma ) ); }\n"
		"float4 ImageFilterMain( VO i ) : SV_Target\n"
		"{\n"
		"	int type = g_Op.x;\n"
		"	bool premultiplied = g_Op.y != 0 && type != 11 && type != 12;\n"
		"	int2 pixel = int2( i.Position.xy ) - g_Where.zw;\n"
		"	int2 source = pixel + g_Where.xy;\n"
		"	float4 c = FetchBase( source );\n"
		"	if ( premultiplied ) { c = Unpremultiply( c ); }\n"
		"	float4 o = c;\n"
		"	if ( type == 0 )\n"
		"	{\n"
		"		float y = Luma( c.rgb );\n"
		"		float cb = g_ArgsA.x / 255.0 * 0.5;\n"
		"		float cr = g_ArgsA.y / 255.0 * 0.5;\n"
		"		o.rgb = saturate( float3( y + 1.402 * cr, y - 0.344136 * cb - 0.714136 * cr, y + 1.772 * cb ) );\n"
		"	}\n"
		"	else if ( type == 1 )\n"
		"	{\n"
		"		float sigma = max( 0.1436 * pow( max( g_ArgsA.y, 1 ), 0.544 ), 0.2 );\n"
		"		int reach = g_ArgsA.x / 2;\n"
		"		float4 sum = 0;\n"
		"		float total = 0;\n"
		"		[loop] for ( int y = -reach; y <= reach; ++y )\n"
		"		{\n"
		"			[loop] for ( int x = -reach; x <= reach; ++x )\n"
		"			{\n"
		"				float w = GaussWeight( x, sigma ) * GaussWeight( y, sigma );\n"
		"				sum += w * FetchBase( source + int2( x, y ) );\n"
		"				total += w;\n"
		"			}\n"
		"		}\n"
		"		o = sum / total;\n"
		"	}\n"
		"	else if ( type == 2 )\n"
		"	{\n"
		"		int n = max( g_ArgsA.x, 1 );\n"
		"		float4 sum = 0;\n"
		"		[loop] for ( int y = 0; y < n; ++y )\n"
		"		{\n"
		"			[loop] for ( int x = 0; x < n; ++x ) { sum += FetchBase( g_Where.xy + pixel * n + int2( x, y ) ); }\n"
		"		}\n"
		"		o = sum / ( n * n );\n"
		"	}\n"
		"	else if ( type == 3 )\n"
		"	{\n"
		"		float luminance = Luma( c.rgb ) * 255.0;\n"
		"		bool clip = g_ArgsA.x != 0 ? luminance < g_ArgsA.y : luminance > g_ArgsA.y;\n"
		"		if ( clip )\n"
		"		{\n"
		"			if ( g_ArgsA.z == 0 ) { discard; }\n"
		"			o = float4( UnpackRgb( g_ArgsA.w ), g_ArgsB.x / 255.0 );\n"
		"		}\n"
		"	}\n"
		"	else if ( type == 4 )\n"
		"	{\n"
		"		float low = g_ArgsA.x / 255.0;\n"
		"		float high = g_ArgsA.y / 255.0;\n"
		"		if ( low <= high ) { o.rgb = saturate( ( c.rgb - low ) / max( high - low, 1.0 / 255.0 ) ); }\n"
		"		else { o.rgb = 1.0 - saturate( ( c.rgb - high ) / max( low - high, 1.0 / 255.0 ) ); }\n"
		"	}\n"
		"	else if ( type == 5 )\n"
		"	{\n"
		"		float3 hsv = RgbToHsv( c.rgb );\n"
		"		hsv.x = frac( ( g_ArgsA.x == 0 ? hsv.x * 360.0 + g_ArgsA.y : g_ArgsA.y ) / 360.0 );\n"
		"		hsv.y = saturate( hsv.y * ( 1.0 + g_ArgsA.z / 255.0 ) );\n"
		"		o.rgb = saturate( HsvToRgb( hsv ) + g_ArgsA.w / 255.0 );\n"
		"	}\n"
		"	else if ( type == 6 )\n"
		"	{\n"
		"		o.rgb = 1.0 - c.rgb;\n"
		"	}\n"
		"	else if ( type == 7 )\n"
		"	{\n"
		"		float3 t = saturate( ( c.rgb - g_ArgsA.x / 255.0 ) / max( ( g_ArgsA.y - g_ArgsA.x ) / 255.0, 1.0 / 255.0 ) );\n"
		"		t = pow( t, 100.0 / max( g_ArgsA.z, 1 ) );\n"
		"		o.rgb = g_ArgsA.w / 255.0 + t * ( g_ArgsB.x - g_ArgsA.w ) / 255.0;\n"
		"	}\n"
		"	else if ( type == 8 )\n"
		"	{\n"
		"		bool low = Luma( c.rgb ) * 255.0 < g_ArgsA.x;\n"
		"		o = low ? float4( UnpackRgb( g_ArgsA.y ), g_ArgsA.z / 255.0 ) : float4( UnpackRgb( g_ArgsA.w ), g_ArgsB.x / 255.0 );\n"
		"	}\n"
		"	else if ( type == 9 )\n"
		"	{\n"
		"		uint mw, mh;\n"
		"		g_Aux.GetDimensions( mw, mh );\n"
		"		float position = min( floor( saturate( Luma( c.rgb ) ) * mw ), mw - 1 );\n"
		"		if ( g_ArgsA.x != 0 ) { position = ( mw - 1 ) - position; }\n"
		"		int index = int( floor( position ) );\n"
		"		float4 map = FetchAux( int2( index, 0 ) );\n"
		"		o = float4( map.rgb, c.a );\n"
		"	}\n"
		"	else if ( type == 10 )\n"
		"	{\n"
		"		int4 now = int4( round( c * 255.0 ) );\n"
		"		if ( all( now == int4( g_ArgsA.x, g_ArgsA.y, g_ArgsA.z, g_ArgsA.w ) ) ) { o = float4( g_ArgsB ) / 255.0; }\n"
		"	}\n"
		"	else if ( type == 11 )\n"
		"	{\n"
		"		o.rgb = c.rgb * c.a;\n"
		"	}\n"
		"	else if ( type == 12 )\n"
		"	{\n"
		"		o = Unpremultiply( c );\n"
		"	}\n"
		"	else if ( type == 13 || type == 14 )\n"
		"	{\n"
		"		o = ScaleSample( pixel, type == 14 );\n"
		"	}\n"
		"	else if ( type == 15 )\n"
		"	{\n"
		"		o = saturate( ( c - asfloat( g_ArgsB ) ) * asfloat( g_ArgsA ) );\n"
		"	}\n"
		"	if ( premultiplied ) { o.rgb *= o.a; }\n"
		"	return o;\n"
		"}\n"
		"float3 BlendColor( int type, float3 b, float3 l )\n"
		"{\n"
		"	if ( type == 2 ) { return b * l; }\n"
		"	if ( type == 3 ) { return saturate( b - l ); }\n"
		"	if ( type == 4 ) { return saturate( b + l ); }\n"
		"	if ( type == 5 ) { return 1.0 - ( 1.0 - b ) * ( 1.0 - l ); }\n"
		"	if ( type == 6 ) { return b < 0.5 ? 2.0 * b * l : 1.0 - 2.0 * ( 1.0 - b ) * ( 1.0 - l ); }\n"
		"	if ( type == 7 ) { return saturate( b / max( 1.0 - l, 1.0 / 255.0 ) ); }\n"
		"	if ( type == 8 ) { return 1.0 - saturate( ( 1.0 - b ) / max( l, 1.0 / 255.0 ) ); }\n"
		"	if ( type == 9 ) { return min( b, l ); }\n"
		"	if ( type == 10 ) { return max( b, l ); }\n"
		"	if ( type == 11 ) { return ( 1.0 - 2.0 * l ) * b * b + 2.0 * l * b; }\n"
		"	if ( type == 12 ) { return l < 0.5 ? 2.0 * b * l : 1.0 - 2.0 * ( 1.0 - b ) * ( 1.0 - l ); }\n"
		"	if ( type == 13 ) { return b + l - 2.0 * b * l; }\n"
		"	return b;\n"
		"}\n"
		"float SelectComponent( int k, float4 b, float4 l )\n"
		"{\n"
		"	if ( k < 4 ) { return b[k]; }\n"
		"	if ( k < 8 ) { return l[k - 4]; }\n"
		"	if ( k < 12 ) { return 1.0 - b[k - 8]; }\n"
		"	return 1.0 - l[k - 12];\n"
		"}\n"
		"float4 ImageBlendMain( VO i ) : SV_Target\n"
		"{\n"
		"	int type = g_Op.x;\n"
		"	float ratio = g_Op.z / 255.0;\n"
		"	int2 pixel = int2( i.Position.xy ) - g_Where.zw;\n"
		"	float4 b = FetchBase( pixel );\n"
		"	float4 l = FetchAux( pixel );\n"
		"	if ( g_Op.y != 0 ) { b = Unpremultiply( b ); l = Unpremultiply( l ); }\n"
		"	float4 o = b;\n"
		"	if ( type == 0 || type == 17 )\n"
		"	{\n"
		"		o.rgb = lerp( b.rgb, l.rgb, l.a * ratio );\n"
		"	}\n"
		"	else if ( type == 1 )\n"
		"	{\n"
		"		float4 picked = float4( SelectComponent( g_ArgsA.x, b, l ), SelectComponent( g_ArgsA.y, b, l ), SelectComponent( g_ArgsA.z, b, l ), SelectComponent( g_ArgsA.w, b, l ) );\n"
		"		o = lerp( b, picked, ratio );\n"
		"	}\n"
		"	else if ( type >= 2 && type <= 13 )\n"
		"	{\n"
		"		o.rgb = lerp( b.rgb, BlendColor( type, b.rgb, l.rgb ), ratio );\n"
		"	}\n"
		"	else if ( type == 14 )\n"
		"	{\n"
		"		float la = l.a * ratio;\n"
		"		o.a = la + b.a * ( 1.0 - la );\n"
		"		o.rgb = ( l.rgb * la + b.rgb * b.a * ( 1.0 - la ) ) / max( o.a, 1.0e-5 );\n"
		"	}\n"
		"	else if ( type == 15 )\n"
		"	{\n"
		"		o.rgb = saturate( b.rgb + l.rgb * l.a * ratio );\n"
		"		o.a = saturate( b.a + l.a * ratio );\n"
		"	}\n"
		"	else if ( type == 16 )\n"
		"	{\n"
		"		o.a = b.a * lerp( 1.0, l.a, ratio );\n"
		"	}\n"
		"	if ( g_Op.y != 0 ) { o.rgb *= o.a; }\n"
		"	return o;\n"
		"}\n"
		// 画素ごとの光: 頂点シェーダー（PixelLitVS）と、ピクセルシェーダー（PixelLitMain / PixelLitTexturedMain）。b6 = 光・材質・カメラ。式は CVertexLighting と同じ
		"struct PLO { float4 Position : SV_POSITION; float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; float3 World : TEXCOORD1; float3 Normal : TEXCOORD2; };\n"
		"PLO PixelLitVS( VI i ) { PLO o; o.Position = i.SubPosition; o.Diffuse = i.Diffuse; o.Specular = i.Specular; o.Uv = i.Uv; o.World = i.Position; o.Normal = i.Normal; return o; }\n"
		"cbuffer cbPixelLight : register( b6 )\n"
		"{\n"
		"	float4 g_LightEye;\n"
		"	float4 g_LightBase;\n"
		"	float4 g_LightMaterialDiffuse;\n"
		"	float4 g_LightMaterialSpecular;\n"
		"	float4 g_LightMaterialAmbient;\n"
		"	float4 g_LightOptions;\n"
		"	float4 g_LightBright;\n"
		"	float4 g_Lights[42];\n"
		"};\n"
		"float3 PixelLightColor( PLO i )\n"
		"{\n"
		"	float3 n = normalize( i.Normal );\n"
		"	float3 toEye = normalize( g_LightEye.xyz - i.World );\n"
		"	float3 diffuseColor = g_LightMaterialDiffuse.a > 0.5 ? i.Diffuse.rgb : g_LightMaterialDiffuse.rgb;\n"
		"	float3 specularColor = g_LightMaterialSpecular.a > 0.5 ? i.Specular.rgb : g_LightMaterialSpecular.rgb;\n"
		"	float3 materialAmbient = g_LightMaterialAmbient.rgb;\n"
		"	float3 sum = g_LightBase.rgb;\n"
		"	int count = int( g_LightEye.w );\n"
		"	[loop] for ( int k = 0; k < count; ++k )\n"
		"	{\n"
		"		float4 position = g_Lights[k * 7];\n"
		"		float4 direction = g_Lights[k * 7 + 1];\n"
		"		float4 attenuation = g_Lights[k * 7 + 2];\n"
		"		float4 spot = g_Lights[k * 7 + 3];\n"
		"		float3 lightDiffuse = g_Lights[k * 7 + 4].rgb;\n"
		"		float3 lightSpecular = g_Lights[k * 7 + 5].rgb;\n"
		"		float3 lightAmbient = g_Lights[k * 7 + 6].rgb;\n"
		"		int type = int( position.w );\n"
		"		float3 toLight = normalize( -direction.xyz );\n"
		"		float reach = 1.0;\n"
		"		if ( type != 0 )\n"
		"		{\n"
		"			float3 offset = i.World - position.xyz;\n"
		"			float dist = length( offset );\n"
		"			toLight = normalize( -offset );\n"
		"			if ( dist > direction.w )\n"
		"			{\n"
		"				reach = 0.0;\n"
		"			}\n"
		"			else\n"
		"			{\n"
		"				reach = 1.0 / ( attenuation.x + attenuation.y * dist + attenuation.z * dist * dist );\n"
		"				if ( type == 2 )\n"
		"				{\n"
		"					float cosAngle = dot( direction.xyz, offset ) / max( dist, 1.0e-6 );\n"
		"					reach *= cosAngle <= attenuation.w ? 0.0 : ( cosAngle >= spot.x ? 1.0 : ( cosAngle - attenuation.w ) / ( spot.x - attenuation.w ) );\n"
		"				}\n"
		"			}\n"
		"		}\n"
		"		if ( reach == 0.0 ) { continue; }\n"
		"		float d = dot( n, toLight );\n"
		"		float angle = max( d, 0.0 );\n"
		"		if ( g_LightOptions.z > 0.5 ) { angle = 1.0; }\n"
		"		else if ( g_LightOptions.y > 0.5 ) { float w = d * 0.5 + 0.5; angle = w * w; }\n"
		"		float highlight = 0.0;\n"
		"		if ( g_LightOptions.x > 0.5 && d > 0.0 )\n"
		"		{\n"
		"			float b = dot( n, normalize( toLight + toEye ) );\n"
		"			if ( b > 0.0 ) { highlight = pow( b, g_LightMaterialAmbient.a ); }\n"
		"		}\n"
		"		sum += reach * ( diffuseColor * lightDiffuse * angle + materialAmbient * lightAmbient + specularColor * lightSpecular * highlight );\n"
		"	}\n"
		"	return saturate( sum ) * g_LightBright.rgb;\n"
		"}\n"
		"float4 PixelLitMain( PLO i ) : SV_Target { return float4( PixelLightColor( i ), i.Diffuse.a ); }\n"
		"float4 PixelLitTexturedMain( PLO i ) : SV_Target { float4 c = float4( PixelLightColor( i ), i.Diffuse.a ) * g_Base.Sample( g_Sampler, i.Uv ); if ( c.a <= 0.0 ) { discard; } return c; }\n";

	uint32 RoundUp( uint32 Value, uint32 Unit )
	{
		return ( Value + Unit - 1 ) / Unit * Unit;
	}
}

CDX12ShaderService::~CDX12ShaderService()
{
	Shutdown();
}

void CDX12ShaderService::Initialize( IGraphicsPipelineResources* InPipelines, IRenderBufferResources* InBuffers, CTextureAssetStore* InTextures )
{
	if ( Pipelines || !InPipelines || !InBuffers || !InTextures )
	{
		return;
	}
	Pipelines = InPipelines;
	Buffers = InBuffers;
	Textures = InTextures;
}

void CDX12ShaderService::Shutdown()
{
	if ( Pipelines )
	{
		for ( const auto& Pair : PipelineCache )
		{
			if ( Pair.Value.Handle.IsValid() )
			{
				Pipelines->ReleaseGraphicsPipeline( Pair.Value.Handle );
			}
		}
	}
	PipelineCache.Reset();
	if ( Textures && EmptyTexture >= 0 )
	{
		Textures->Delete( EmptyTexture );
	}
	EmptyTexture = -1;
	Shaders.Reset();
	BuiltInRect = FShaderRecord();
	BuiltInQuad = FShaderRecord();
	BuiltInLit = FShaderRecord();
	BuiltInShadowDepth = FShaderRecord();
	BuiltInShadowLit = FShaderRecord();
	BuiltInShadowLitTextured = FShaderRecord();
	BuiltInFogTextured = FShaderRecord();
	BuiltInImageFilter = FShaderRecord();
	BuiltInImageBlend = FShaderRecord();
	BuiltInPixelLitVertex = FShaderRecord();
	BuiltInPixelLit = FShaderRecord();
	BuiltInPixelLitTextured = FShaderRecord();
	ShadowPasses.Reset();
	bBuiltInTried = false;
	Pipelines = nullptr;
	Buffers = nullptr;
	Textures = nullptr;
}

bool CDX12ShaderService::Reflect( EShaderStage Stage, const uint8* Code, int32 NumBytes, FShaderRecord& Record ) const
{
	if ( !Code || NumBytes < 32 || NumBytes > 16 * 1024 * 1024 )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection; // 成功するまで、出力を書き換えない。
	D3D12_SHADER_DESC Desc = {};
	if ( FAILED( D3DReflect( Code, static_cast<SIZE_T>( NumBytes ), IID_PPV_ARGS( &Reflection ) ) ) || FAILED( Reflection->GetDesc( &Desc ) ) )
	{
		return false;
	}
	const UINT Type = D3D12_SHVER_GET_TYPE( Desc.Version ); // 0 = ピクセル、1 = 頂点。
	if ( Type != ( Stage == EShaderStage::Vertex ? 1u : 0u ) || D3D12_SHVER_GET_MAJOR( Desc.Version ) != 5 )
	{
		return false;
	}
	FShaderRecord Result;
	Result.Stage = Stage;
	Result.Code.Append( Code, NumBytes );
	for ( UINT Index = 0; Index < Desc.InputParameters && Stage == EShaderStage::Vertex; ++Index )
	{
		D3D12_SIGNATURE_PARAMETER_DESC Param = {};
		if ( FAILED( Reflection->GetInputParameterDesc( Index, &Param ) ) )
		{
			return false;
		}
		if ( Param.SystemValueType != D3D_NAME_UNDEFINED )
		{
			continue; // SV_VertexID などは、頂点の中身を使わない。
		}
		FInput Input;
		Input.Semantic = FString( Param.SemanticName );
		Input.Index = Param.SemanticIndex;
		Input.Mask = Param.Mask;
		Result.Inputs.Add( Input );
	}
	for ( UINT Index = 0; Index < Desc.BoundResources; ++Index )
	{
		D3D12_SHADER_INPUT_BIND_DESC Bind = {};
		if ( FAILED( Reflection->GetResourceBindingDesc( Index, &Bind ) ) )
		{
			return false;
		}
		if ( Bind.Type == D3D_SIT_SAMPLER )
		{
			continue;
		}
		if ( Bind.Type == D3D_SIT_CBUFFER )
		{
			ID3D12ShaderReflectionConstantBuffer* Buffer = Reflection->GetConstantBufferByName( Bind.Name );
			D3D12_SHADER_BUFFER_DESC BufferDesc = {};
			if ( !Buffer || FAILED( Buffer->GetDesc( &BufferDesc ) ) )
			{
				return false;
			}
			FConstantInfo Info;
			Info.Binding = Bind.BindPoint;
			Info.Size = BufferDesc.Size;
			Result.Constants.Add( Info );
		}
		else if ( Bind.Type == D3D_SIT_TEXTURE && Bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2D )
		{
			Result.Textures.Add( Bind.BindPoint );
		}
		else
		{
			Result.bUnsupportedResource = true;   // 構造化バッファ・Texture3D など。
		}
	}
	for ( UINT Index = 0; Index < Desc.OutputParameters && Stage == EShaderStage::Pixel; ++Index )
	{
		D3D12_SIGNATURE_PARAMETER_DESC Param = {};
		if ( SUCCEEDED( Reflection->GetOutputParameterDesc( Index, &Param ) ) && Param.SystemValueType == D3D_NAME_TARGET )
		{
			++Result.NumOutputs;
		}
	}
	Record = MoveTemp( Result );
	return true;
}

int32 CDX12ShaderService::Make( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	FShaderRecord Record;
	if ( !Reflect( Stage, static_cast<const uint8*>( Code ), NumBytes, Record ) || NextShader == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	const int32 Handle = NextShader++;   // 番号は使い回さない。
	Shaders.Add( Handle, MoveTemp( Record ) );
	return Handle;
}

void CDX12ShaderService::Delete( int32 Shader )
{
	if ( !Shaders.Find( Shader ) )
	{
		return;
	}
	TArray<FString> Stale; // 走査中に表を変えないよう、先に鍵を集める。
	for ( const auto& Pair : PipelineCache )
	{
		if ( Pair.Value.VertexShader == Shader || Pair.Value.PixelShader == Shader )
		{
			Stale.Add( Pair.Key );
		}
	}
	for ( const FString& Key : Stale )
	{
		if ( const FPipelineEntry* Entry = PipelineCache.Find( Key ) )
		{
			if ( Pipelines && Entry->Handle.IsValid() )
			{
				Pipelines->ReleaseGraphicsPipeline( Entry->Handle );
			}
		}
		PipelineCache.Remove( Key );
	}
	Shaders.Remove( Shader );
}

bool CDX12ShaderService::Compile( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage )
{
	OutCode.Reset();
	OutMessage = FString();
	if ( !Source || NumBytes < 1 || !EntryPoint || EntryPoint[0] == '\0' || !ShaderModel )
	{
		OutMessage = "the source, the entry point and the shader model are required";
		return false;
	}
	if ( NCString::Compare( ShaderModel, "4_0" ) != 0 && NCString::Compare( ShaderModel, "4_1" ) != 0 && NCString::Compare( ShaderModel, "5_0" ) != 0 )
	{
		OutMessage = "the shader model must be 4_0, 4_1 or 5_0";
		return false;
	}
	// DX12 は Shader Model 5 だけを受け取る。4_0 / 4_1 のソースも、5_0 で同じようにコンパイルできる。
	const char* Target = Stage == EShaderStage::Pixel ? "ps_5_0" : "vs_5_0";
	Microsoft::WRL::ComPtr<ID3DBlob> Code, Errors;
	const HRESULT Result = D3DCompile( Source, static_cast<SIZE_T>( NumBytes ), SourceName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoint, Target, 0, 0, &Code, &Errors );
	if ( Errors )
	{
		OutMessage = FString( static_cast<const char*>( Errors->GetBufferPointer() ), static_cast<int32>( Errors->GetBufferSize() ) );
	}
	if ( FAILED( Result ) || !Code )
	{
		if ( OutMessage.IsEmpty() )
		{
			OutMessage = "the shader cannot be compiled";
		}
		return false;
	}
	OutCode.Append( static_cast<const uint8*>( Code->GetBufferPointer() ), static_cast<int32>( Code->GetBufferSize() ) );
	return true;
}

bool CDX12ShaderService::IsShaderValid( int32 Shader, EShaderStage Stage ) const
{
	const FShaderRecord* Record = Shaders.Find( Shader );
	return Record && Record->Stage == Stage;
}

void CDX12ShaderService::BuildBuiltIns()
{
	if ( bBuiltInTried )
	{
		return;
	}
	bBuiltInTried = true;
	const int32 Length = static_cast<int32>( NCString::Length( BuiltInSource ) );
	const auto Build = [&]( EShaderStage Stage, const char* Entry, FShaderRecord& Out )
	{
		TArray<uint8> Code;
		FString Message;
		if ( !Compile( Stage, BuiltInSource, Length, "BuiltInShader", Entry, "5_0", Code, Message ) || !Reflect( Stage, Code.GetData(), Code.Num(), Out ) )
		{
			LOG( LogDX12, Warning, "CDX12ShaderService: the built-in shader '%s' cannot be built: %s", Entry, *Message );
		}
	};
	Build( EShaderStage::Vertex, "RectMain", BuiltInRect );
	Build( EShaderStage::Vertex, "QuadMain", BuiltInQuad );
	Build( EShaderStage::Vertex, "LitMain", BuiltInLit );
	Build( EShaderStage::Pixel, "ShadowDepthMain", BuiltInShadowDepth );
	Build( EShaderStage::Pixel, "ShadowLitMain", BuiltInShadowLit );
	Build( EShaderStage::Pixel, "ShadowLitTexturedMain", BuiltInShadowLitTextured );
	Build( EShaderStage::Pixel, "FogTexturedMain", BuiltInFogTextured );
	Build( EShaderStage::Pixel, "ImageFilterMain", BuiltInImageFilter );
	Build( EShaderStage::Pixel, "ImageBlendMain", BuiltInImageBlend );
	Build( EShaderStage::Vertex, "PixelLitVS", BuiltInPixelLitVertex );
	Build( EShaderStage::Pixel, "PixelLitMain", BuiltInPixelLit );
	Build( EShaderStage::Pixel, "PixelLitTexturedMain", BuiltInPixelLitTextured );
}

const CDX12ShaderService::FShaderRecord* CDX12ShaderService::GetBuiltIn( EBuiltInVertexShader Kind )
{
	BuildBuiltIns();
	const FShaderRecord* Record = Kind == EBuiltInVertexShader::Rect ? &BuiltInRect : Kind == EBuiltInVertexShader::Quad3D ? &BuiltInQuad : Kind == EBuiltInVertexShader::ShadowLit ? &BuiltInLit : Kind == EBuiltInVertexShader::PixelLit ? &BuiltInPixelLitVertex : nullptr;
	return Record && !Record->Code.IsEmpty() ? Record : nullptr;
}

const CDX12ShaderService::FShaderRecord* CDX12ShaderService::GetBuiltInPixel( EBuiltInPixelShader Kind )
{
	BuildBuiltIns();
	const FShaderRecord* Record = Kind == EBuiltInPixelShader::ShadowDepth ? &BuiltInShadowDepth : Kind == EBuiltInPixelShader::ShadowLit ? &BuiltInShadowLit : Kind == EBuiltInPixelShader::ShadowLitTextured ? &BuiltInShadowLitTextured : Kind == EBuiltInPixelShader::FogTextured ? &BuiltInFogTextured : Kind == EBuiltInPixelShader::ImageFilter ? &BuiltInImageFilter : Kind == EBuiltInPixelShader::ImageBlend ? &BuiltInImageBlend : Kind == EBuiltInPixelShader::PixelLit ? &BuiltInPixelLit : Kind == EBuiltInPixelShader::PixelLitTextured ? &BuiltInPixelLitTextured : nullptr;
	return Record && !Record->Code.IsEmpty() ? Record : nullptr;
}

void CDX12ShaderService::SetShadowPass( int32 ShadowMap, const FMatrix4x4& LightViewProjection, float32 AdjustDepth, int32 Width, int32 Height )
{
	FShadowPass Pass;
	Pass.LightViewProjection = LightViewProjection;
	Pass.AdjustDepth = AdjustDepth;
	Pass.Width = Width;
	Pass.Height = Height;
	ShadowPasses.Add( ShadowMap, Pass );
}

bool CDX12ShaderService::GetShadowPass( int32 ShadowMap, FMatrix4x4& OutLightViewProjection, float32& OutAdjustDepth, int32& OutWidth, int32& OutHeight ) const
{
	const FShadowPass* Pass = ShadowPasses.Find( ShadowMap );
	if ( !Pass )
	{
		return false;
	}
	OutLightViewProjection = Pass->LightViewProjection;
	OutAdjustDepth = Pass->AdjustDepth;
	OutWidth = Pass->Width;
	OutHeight = Pass->Height;
	return true;
}

bool CDX12ShaderService::GetEmptyTexture( FTextureResourceHandle& Out )
{
	if ( EmptyTexture < 0 || !Textures->GetResource( EmptyTexture, Out ) )
	{
		const FColor32 Clear( 0, 0, 0, 0 );   // 読むと 0 になる、1 x 1 の透明な黒
		EmptyTexture = Textures->Create( 1, 1, &Clear );
		if ( EmptyTexture < 0 || !Textures->GetResource( EmptyTexture, Out ) )
		{
			return false;
		}
	}
	return true;
}

FGraphicsPipelineHandle CDX12ShaderService::GetPipeline( const FShaderDrawRequest& Request, const FShaderRecord& Vertex, const FShaderRecord& Pixel, const TArray<FGraphicsBufferSlot>& BufferSlots, const TArray<FGraphicsTextureSlot>& TextureSlots )
{
	const FGeometryDrawState& State = Request.State;
	const int32 VertexId = Request.Builtin == EBuiltInVertexShader::None ? Request.VertexShader : -2 - static_cast<int32>( Request.Builtin );
	const int32 PixelId = Request.BuiltinPixel == EBuiltInPixelShader::None ? Request.PixelShader : -2 - static_cast<int32>( Request.BuiltinPixel );
	FString Formats; // 書き込み先と読む画像の形式（パイプラインの形を決める）。
	for ( int32 Target = 0; Target < Request.NumTargets; ++Target )
	{
		Formats += static_cast<char>( '0' + static_cast<int32>( Request.TargetFormats[Target] ) );
	}
	Formats += '/';
	for ( const FGraphicsTextureSlot& Slot : TextureSlots )
	{
		Formats += static_cast<char>( '0' + static_cast<int32>( Slot.Format ) );
	}
	const FString Key = Formats + FString::Printf( "|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", VertexId, PixelId, static_cast<int32>( Request.Topology ), Request.bUseDepth ? 1 : 0,
		State.bTestDepth ? 1 : 0, State.bWriteDepth ? 1 : 0, static_cast<int32>( State.Compare ), State.DepthBias, static_cast<int32>( State.Culling ), State.bAlphaBlend ? 1 : 0,
		State.bLinearFilter ? 1 : 0, static_cast<int32>( State.AddressU ), static_cast<int32>( State.AddressV ), State.bAlphaBlend ? static_cast<int32>( State.Blend ) : 0 ) + ( State.bAlphaBlend && State.Blend == EGeometryBlend::Custom ? FString::Printf( "|%u", State.CustomBlend.Pack() ) : FString() );
	if ( const FPipelineEntry* Found = PipelineCache.Find( Key ) )
	{
		return Found->Handle;   // 作れなかった組み合わせは、無効なまま覚えて、毎回作り直さない。
	}
	// 頂点シェーダーの入力を、頂点の中の位置へ割り当てる。
	TArray<FGraphicsInputElement> Elements;
	for ( const FInput& Input : Vertex.Inputs )
	{
		FGraphicsInputElement Element;
		if ( !MapSemantic( Input.Semantic, Input.Index, Element.Format, Element.AlignedByteOffset ) || ( Input.Mask & ~GetFormatMask( Element.Format ) ) != 0 )
		{
			LOG( LogDX12, Warning, "CDX12ShaderService: the vertex shader input %s%u cannot be read from the vertex (declare the inputs of the shader vertex in order)", *Input.Semantic, Input.Index );
			PipelineCache.Add( Key, FPipelineEntry() );
			return FGraphicsPipelineHandle();
		}
		Element.SemanticName = *Input.Semantic;
		Element.SemanticIndex = Input.Index;
		Elements.Add( Element );
	}
	FGraphicsPipelineDesc Desc;
	Desc.VertexShaderBytecode = TArrayView<const uint8>( Vertex.Code.GetData(), Vertex.Code.Num() );
	Desc.PixelShaderBytecode = TArrayView<const uint8>( Pixel.Code.GetData(), Pixel.Code.Num() );
	Desc.VertexStride = FShaderDrawRequest::VertexStride;
	Desc.InputElements = TArrayView<const FGraphicsInputElement>( Elements.GetData(), Elements.Num() );
	Desc.BufferSlots = TArrayView<const FGraphicsBufferSlot>( BufferSlots.GetData(), BufferSlots.Num() );
	Desc.TextureSlots = TArrayView<const FGraphicsTextureSlot>( TextureSlots.GetData(), TextureSlots.Num() );
	Desc.NumRenderTargets = static_cast<uint32>( Request.NumTargets );
	for ( int32 Target = 0; Target < Request.NumTargets; ++Target )
	{
		Desc.RenderTargetFormats[Target] = Request.TargetFormats[Target];
	}
	Desc.DepthFormat = Request.bUseDepth ? EGraphicsDepthFormat::D32Float : EGraphicsDepthFormat::None;
	Desc.Topology = Request.Topology;
	Desc.DepthCompare = State.Compare;
	Desc.Culling = State.Culling;
	Desc.SamplerAddress = State.AddressU;
	Desc.bDepthTest = Request.bUseDepth && State.bTestDepth;
	Desc.bDepthWrite = Request.bUseDepth && State.bTestDepth && State.bWriteDepth;
	Desc.bAlphaBlend = State.bAlphaBlend;
	Desc.Blend = State.Blend;
	Desc.CustomBlend = State.CustomBlend;
	Desc.bLinearSampler = State.bLinearFilter;
	FPipelineEntry Entry;
	Entry.VertexShader = Request.Builtin == EBuiltInVertexShader::None ? Request.VertexShader : -1;
	Entry.PixelShader = Request.BuiltinPixel == EBuiltInPixelShader::None ? Request.PixelShader : -1;
	Entry.Handle = Pipelines->CreateGraphicsPipeline( Desc );
	if ( !Entry.Handle.IsValid() )
	{
		LOG( LogDX12, Warning, "CDX12ShaderService: the pipeline of the shaders (vertex %d, pixel %d) cannot be created (the shaders must be Shader Model 5, the vertex inputs must be readable, and the outputs of the vertex shader must cover the inputs of the pixel shader)", VertexId, PixelId );
	}
	PipelineCache.Add( Key, Entry );
	return Entry.Handle;
}

bool CDX12ShaderService::DrawShader( const FShaderDrawRequest& Request, IRenderGeometry3D& Geometry )
{
	if ( !Pipelines || !Buffers || !Textures || !Request.Vertices || Request.NumVertices <= 0 || Request.NumVertices > 65536 || ( Request.Indices && ( Request.NumIndices <= 0 || Request.NumIndices > 196608 ) ) )
	{
		return false;
	}
	const FShaderRecord* Vertex = Request.Builtin == EBuiltInVertexShader::None ? Shaders.Find( Request.VertexShader ) : GetBuiltIn( Request.Builtin );
	const FShaderRecord* Pixel = Request.BuiltinPixel == EBuiltInPixelShader::None ? Shaders.Find( Request.PixelShader ) : GetBuiltInPixel( Request.BuiltinPixel );
	if ( !Vertex || !Pixel || Vertex->Stage != EShaderStage::Vertex || Pixel->Stage != EShaderStage::Pixel )
	{
		return false;
	}
	if ( Vertex->bUnsupportedResource || Pixel->bUnsupportedResource || !Vertex->Textures.IsEmpty() )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader resources", "Only constant buffers and Texture2D in the pixel shader are supported" );
		return false;
	}
	if ( Request.NumTargets < 1 || Request.NumTargets > 8 || Pixel->NumOutputs != Request.NumTargets )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader outputs", "The number of outputs of the pixel shader (SV_Target) must match the number of render targets" );
		return false;
	}
	// 置き場: 頂点・ピクセルの両方が読む定数バッファ（b 番号で 1 つにまとめる）と、ピクセルシェーダーが読む画像
	TArray<FGraphicsBufferSlot> BufferSlots;
	TArray<uint32> BufferSizes; // 読まれる大きさの最大。
	TArray<bool> BufferReadByVertex; // その番号を、頂点シェーダーが読むか（読まなければ、ピクセル側の中身を使う）。
	const auto AddConstant = [&]( const FConstantInfo& Info, bool bVertexStage )
	{
		for ( int32 Index = 0; Index < BufferSlots.Num(); ++Index )
		{
			if ( BufferSlots[Index].Binding == Info.Binding )
			{
				BufferSizes[Index] = NMath::Max( BufferSizes[Index], Info.Size );
				BufferReadByVertex[Index] = BufferReadByVertex[Index] || bVertexStage;
				return;
			}
		}
		FGraphicsBufferSlot Slot;
		Slot.Binding = Info.Binding;
		Slot.Layout = EGraphicsBufferLayout::Constant;
		BufferSlots.Add( Slot );
		BufferSizes.Add( Info.Size );
		BufferReadByVertex.Add( bVertexStage );
	};
	for ( const FConstantInfo& Info : Vertex->Constants )
	{
		AddConstant( Info, true );
	}
	for ( const FConstantInfo& Info : Pixel->Constants )
	{
		AddConstant( Info, false );
	}
	TArray<FGraphicsTextureSlot> TextureSlots;
	for ( const uint32 Binding : Pixel->Textures )
	{
		FGraphicsTextureSlot Slot;
		Slot.Binding = Binding;
		Slot.Format = EComputeTextureFormat::RGBA8; // 段に置いた画像の形式（置いていなければ、空の画像のRGBA8）。
		if ( Binding < static_cast<uint32>( FShaderDrawRequest::NumTextureStages ) && Request.Textures[Binding] >= 0 )
		{
			Textures->GetShaderFormat( Request.Textures[Binding], Slot.Format );
		}
		TextureSlots.Add( Slot );
	}
	const FGraphicsPipelineHandle Pipeline = GetPipeline( Request, *Vertex, *Pixel, BufferSlots, TextureSlots );
	if ( !Pipeline.IsValid() )
	{
		return false;
	}
	// 画像: 段に置いてあるものか、無ければ空の画像
	TArray<FTextureResourceHandle> SlotTextures;
	for ( const FGraphicsTextureSlot& Slot : TextureSlots )
	{
		FTextureResourceHandle Resource;
		const int32 Asset = Slot.Binding < static_cast<uint32>( FShaderDrawRequest::NumTextureStages ) ? Request.Textures[Slot.Binding] : -1;
		if ( Asset < 0 || !Textures->GetResource( Asset, Resource ) )
		{
			if ( !GetEmptyTexture( Resource ) )
			{
				return false;
			}
		}
		SlotTextures.Add( Resource );
	}
	// 定数: 置いてある中身を 256 の倍数へ切り上げ、余りは 0（転送領域へ載せる側が揃えるので、読まれる大きさ以上の中身を渡す）。
	TArray<TArray<uint8>> ConstantBytes;
	TArray<FGraphicsConstantBytes> Constants;
	for ( int32 Index = 0; Index < BufferSlots.Num(); ++Index )
	{
		// 置いてある中身。その番号を、頂点シェーダーが読むなら頂点側、読まないならピクセル側（別の段に置いた古い中身を、読んでしまわないように）。
		// 両方が読む番号で、中身が違うときは、頂点側が使われる。
		const int32 WantedStage = static_cast<int32>( BufferReadByVertex[Index] ? EShaderStage::Vertex : EShaderStage::Pixel );
		const FShaderConstantData* Found = nullptr;
		for ( const FShaderConstantData& Data : Request.Constants )
		{
			if ( static_cast<uint32>( Data.Slot ) == BufferSlots[Index].Binding && ( !Found || ( Data.Stage == WantedStage && Found->Stage != WantedStage ) ) )
			{
				Found = &Data;
			}
		}
		const uint32 Size = NMath::Max( 1u, NMath::Max( BufferSizes[Index], Found ? static_cast<uint32>( Found->Bytes.Num() ) : 0u ) );
		if ( Size > 65536 )
		{
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNum( static_cast<int32>( Size ), 0 );
		if ( Found )
		{
			for ( int32 Byte = 0; Byte < Found->Bytes.Num(); ++Byte )
			{
				Bytes[Byte] = Found->Bytes[Byte];
			}
		}
		ConstantBytes.Add( MoveTemp( Bytes ) );
	}
	for ( const TArray<uint8>& Bytes : ConstantBytes )
	{
		FGraphicsConstantBytes Constant;
		Constant.Data = Bytes.GetData();
		Constant.Size = static_cast<uint32>( Bytes.Num() );
		Constants.Add( Constant );
	}
	FGraphicsPipelineTransientDraw Draw;
	Draw.Pipeline = Pipeline;
	Draw.Vertices = Request.Vertices;
	Draw.VertexCount = static_cast<uint32>( Request.NumVertices );
	Draw.Indices = Request.Indices;
	Draw.IndexCount = Request.Indices ? static_cast<uint32>( Request.NumIndices ) : 0u;
	Draw.Constants = TArrayView<const FGraphicsConstantBytes>( Constants.GetData(), Constants.Num() );
	Draw.Textures = TArrayView<const FTextureResourceHandle>( SlotTextures.GetData(), SlotTextures.Num() );
	const bool bDrawn = Geometry.DrawGraphicsPipelineTransient( Draw );
	if ( !bDrawn )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader draw (rejected by the GPU path)", "DrawGraphicsPipelineTransient rejected the draw (check the pass, formats, constants and textures)" );
	}
	return bDrawn;
}
