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
	// 非一様な拡大や鏡映でも法線が面に垂直になるよう、逆転置で変換する。
	float3 x = g_World[0].xyz, y = g_World[1].xyz, z = g_World[2].xyz; // 個体の変換後の局所軸。
	float3 cx = cross( y, z ), cy = cross( z, x ), cz = cross( x, y ); // 逆転置の分子。
	float determinant = dot( x, cx ); // 鏡映と特異変換の判定。
	float3 normal = ( i.Normal.x * cx + i.Normal.y * cy + i.Normal.z * cz ) * ( determinant < 0.0 ? -1.0 : 1.0 ); // ワールド法線。
	float normalLength = dot( normal, normal ); // 退化した法線の判定。
	// 長さを保って補間し、画素側で正規化する。
	o.Normal = abs( determinant ) > 1.0e-8 && normalLength > 1.0e-12 ? normal : float3( 0, 1, 0 );
	o.World = world.xyz;
	o.Uv = i.Uv;
	o.Color = i.Diffuse * g_Color;
	o.LocalPosition = i.Position;
	o.LocalNormal = i.Normal;
	return o;
}
