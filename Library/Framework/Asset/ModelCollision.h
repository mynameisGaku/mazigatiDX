// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Asset/ModelCollisionTypes.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FModelCollision
//   モデル 1 体（CModelInstance）への当たり判定: 線を飛ばして最初に当たる所（Raycast）、球と重なる三角形（OverlapSphere）。
//   モデルの形は、最初に問い合わせたとき（または Refresh したとき）の形を写して使う（毎回は写さない。写すのは重いので）。
//   アニメーションや形状で形が変わったあとに、その形で調べたいときは Refresh を呼ぶ。
//   置き場所は、問い合わせのたびに World（モデルを描くときと同じ行列）で渡す。移動・回転・拡大は写し直さなくてよい（拡大は縦横高さ同じ倍率）。
//   CModelInstance::GetCollision() で受け取る。インスタンスより長く持たない。
//
//   FModelHit Hit;
//   if ( Rock->GetCollision().Raycast( RockWorld, Eye, Eye + Forward * 100.0f, Hit ) ) { Marker = Hit.Position; }
//=============================================================================
class FModelCollision
{
public:
	explicit FModelCollision( int32 InHandle );

	// いまのモデルの形（アニメーション・形状を含む）を写し直す。メッシュが無い・読み込み中なら false
	bool Refresh();

	// From から To への線（ワールドの座標）が最初に当たる三角形。当たらなければ false（Out は変えない）
	bool Raycast( const FMatrix4x4& World, const FVector3& From, const FVector3& To, FModelHit& Out ) const;

	// 中心 Center・半径 Radius の球（ワールドの座標）と重なる三角形をすべて。Distance は中心から三角形のいちばん近い点まで
	TArray<FModelHit> OverlapSphere( const FMatrix4x4& World, const FVector3& Center, float32 Radius ) const;

private:
	int32 Handle;
};
