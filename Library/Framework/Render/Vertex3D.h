// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Math/Math.h"

//=============================================================================
// FVertex3D
//   FDrawContext::Primitive3D / PrimitiveIndexed3D に渡す頂点 1 つ（DxLib の VERTEX3D）。DxLib に依存しない値。
//     Position  位置（ワールド座標。PushWorldMatrix / WorldMatrix の変換が掛かる）
//     Normal    面の向き。光を受けるときに使う。長さは問わない（DxLib が揃える）。長さ 0 だと、光を受けた面は真っ黒
//     Diffuse   色（R, G, B, A）。画像があれば、画像に掛かる。A は、TransFlag が真の描画（ここでは常に真）で、頂点ごとの不透明度として効く（Blend が Opaque でも効く）
//     Specular  鏡面反射の色。光の計算をするときは、ハイライトの色になり、光の計算を切る（UseLighting( false )）と、Diffuse に足される
//     U, V      画像の座標（0..1。左上が 0, 0）
//     SubU, SubV  固定機能の描画では使われない（シェーダー用）
//=============================================================================
struct FVertex3D
{
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );
	FVector3 Normal = FVector3( 0.0f, 1.0f, 0.0f );
	FColor32 Diffuse = FColor32( 255, 255, 255, 255 );
	FColor32 Specular = FColor32( 0, 0, 0, 0 );
	float32 U = 0.0f;
	float32 V = 0.0f;
	float32 SubU = 0.0f;
	float32 SubV = 0.0f;

	// 位置・向き・色・画像の座標を決めて作る
	static FORCEINLINE FVertex3D Make( const FVector3& InPosition, const FVector3& InNormal, const FColor32& InDiffuse, float32 InU = 0.0f, float32 InV = 0.0f )
	{
		FVertex3D Vertex;
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
			&& NMath::IsFinite( Normal.x ) && NMath::IsFinite( Normal.y ) && NMath::IsFinite( Normal.z )
			&& NMath::IsFinite( U ) && NMath::IsFinite( V ) && NMath::IsFinite( SubU ) && NMath::IsFinite( SubV );
	}

	// 描画コマンドの頂点にする（記録のとき）
	FORCEINLINE FDrawVertex3D ToDrawData() const
	{
		FDrawVertex3D Data;
		Data.Position[0] = Position.x;
		Data.Position[1] = Position.y;
		Data.Position[2] = Position.z;
		Data.Normal[0] = Normal.x;
		Data.Normal[1] = Normal.y;
		Data.Normal[2] = Normal.z;
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
