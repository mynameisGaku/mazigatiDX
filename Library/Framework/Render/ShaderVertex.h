// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Math/Math.h"

//=============================================================================
// FShaderVertex3D
//   FDrawContext::PrimitiveShader3D / PrimitiveIndexedShader3D に渡す頂点 1 つ（DxLib の VERTEX3DSHADER）。DxLib に依存しない値。
//   固定機能の頂点（FVertex3D）と違い、何を入れても、シェーダーが好きな意味で読める（位置のほかの項目は、頂点シェーダーが読んで初めて意味を持つ）。
//   頂点シェーダーが読めるのは、構造体の並びのとおりに、先頭から飛ばさずに宣言した分（CShader の説明を参照）。
//
//   FShaderVertex3D
//     Position     位置（頂点シェーダーが、自分でクリップ座標にする）
//     SubPosition  補助の位置（シェーダーが好きに使える 4 つの数）
//     Normal       面の向き
//     Tangent      接線。法線マップなどで使う
//     Binormal     従法線
//     Diffuse      色（R, G, B, A）。頂点シェーダーには、0..1 の 4 つの数として入る
//     Specular     もう 1 つの色
//     U, V         画像の座標
//     SubU, SubV   もう 1 組の画像の座標
//=============================================================================
struct FShaderVertex3D
{
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );
	FVector4 SubPosition = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	FVector3 Normal = FVector3( 0.0f, 1.0f, 0.0f );
	FVector3 Tangent = FVector3( 1.0f, 0.0f, 0.0f );
	FVector3 Binormal = FVector3( 0.0f, 0.0f, 1.0f );
	FColor32 Diffuse = FColor32( 255, 255, 255, 255 );
	FColor32 Specular = FColor32( 0, 0, 0, 0 );
	float32 U = 0.0f;
	float32 V = 0.0f;
	float32 SubU = 0.0f;
	float32 SubV = 0.0f;

	// 位置・向き・色・画像の座標を決めて作る
	static FORCEINLINE FShaderVertex3D Make( const FVector3& InPosition, const FVector3& InNormal, const FColor32& InDiffuse, float32 InU = 0.0f, float32 InV = 0.0f )
	{
		FShaderVertex3D Vertex;
		Vertex.Position = InPosition;
		Vertex.Normal = InNormal;
		Vertex.Diffuse = InDiffuse;
		Vertex.U = InU;
		Vertex.V = InV;
		return Vertex;
	}

	// 数値がすべて有限か（NaN・無限大が 1 つでもあれば false）。色は 0..255 の整数なので、確かめない
	FORCEINLINE bool IsValid() const
	{
		return NMath::IsFinite( Position.x ) && NMath::IsFinite( Position.y ) && NMath::IsFinite( Position.z )
			&& NMath::IsFinite( SubPosition.x ) && NMath::IsFinite( SubPosition.y ) && NMath::IsFinite( SubPosition.z ) && NMath::IsFinite( SubPosition.w )
			&& NMath::IsFinite( Normal.x ) && NMath::IsFinite( Normal.y ) && NMath::IsFinite( Normal.z )
			&& NMath::IsFinite( Tangent.x ) && NMath::IsFinite( Tangent.y ) && NMath::IsFinite( Tangent.z )
			&& NMath::IsFinite( Binormal.x ) && NMath::IsFinite( Binormal.y ) && NMath::IsFinite( Binormal.z )
			&& NMath::IsFinite( U ) && NMath::IsFinite( V ) && NMath::IsFinite( SubU ) && NMath::IsFinite( SubV );
	}

	// 描画コマンドの頂点にする（記録のとき）
	FORCEINLINE FDrawShaderVertex3D ToDrawData() const
	{
		FDrawShaderVertex3D Data;
		Data.Position[0] = Position.x;
		Data.Position[1] = Position.y;
		Data.Position[2] = Position.z;
		Data.SubPosition[0] = SubPosition.x;
		Data.SubPosition[1] = SubPosition.y;
		Data.SubPosition[2] = SubPosition.z;
		Data.SubPosition[3] = SubPosition.w;
		Data.Normal[0] = Normal.x;
		Data.Normal[1] = Normal.y;
		Data.Normal[2] = Normal.z;
		Data.Tangent[0] = Tangent.x;
		Data.Tangent[1] = Tangent.y;
		Data.Tangent[2] = Tangent.z;
		Data.Binormal[0] = Binormal.x;
		Data.Binormal[1] = Binormal.y;
		Data.Binormal[2] = Binormal.z;
		Data.Diffuse[0] = Diffuse.r;
		Data.Diffuse[1] = Diffuse.g;
		Data.Diffuse[2] = Diffuse.b;
		Data.Diffuse[3] = Diffuse.a;
		Data.Specular[0] = Specular.r;
		Data.Specular[1] = Specular.g;
		Data.Specular[2] = Specular.b;
		Data.Specular[3] = Specular.a;
		Data.U = U;
		Data.V = V;
		Data.SubU = SubU;
		Data.SubV = SubV;
		return Data;
	}
};
