// SPDX-License-Identifier: MIT

struct VertexInput
{
	float3 Position : POSITION;
	float4 SubPosition : POSITION1;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float3 Binormal : BINORMAL;
	float4 Diffuse : COLOR0;
	float4 Specular : COLOR1;
	float2 Uv : TEXCOORD0;
	float2 SubUv : TEXCOORD1;
};

VertexOutput main( VertexInput i )
{
	VertexOutput o;
	float4 world = mul( float4( i.Position, 1 ), g_World );
	o.Position = mul( world, g_ViewProjection );
	o.Normal = mul( i.Normal, ( float3x3 )g_World );
	o.World = world.xyz;
	o.Uv = i.Uv;
	o.Color = i.Diffuse * g_Color;
	return o;
}
