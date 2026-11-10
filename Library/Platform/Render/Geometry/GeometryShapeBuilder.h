// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/DrawVertex3D.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"

// 球とカプセルをローカル座標の三角形・線分へ展開する。GPUや描画状態を持たない。
class CGeometryShapeBuilder
{
public:
	// 円周をDiv分割する球。Divは3〜128、Radiusは0以上の有限値。
	// 半径0なら空の形状で成功。不正値・座標の桁あふれではOutを保ってfalse。
	static bool BuildSphere( const FVector3& Center, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out );
	// AとBを半球の中心とするカプセル。同じ点なら球。失敗の規約は球と同じ。
	static bool BuildCapsule( const FVector3& A, const FVector3& B, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out );
};
