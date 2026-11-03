// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/ModelStructureTypes.h"

class CModel;

//=============================================================================
// FModelAppearance
//   モデル 1 体（CModelInstance）の見た目を変える窓口: 材質の色・メッシュとフレームの表示・全体の不透明度。
//   変えるのはその 1 体だけ（同じモデルから作ったほかのインスタンスや、元のデータは変わらない）。
//   CModelInstance::GetAppearance() で受け取る。インスタンスより長く持たない（ハンドルを借りているだけ）。
//   番号は CModel::GetStructure() の並び。範囲の外・バックエンドが対応していないときは、何もせず false。
//
//   FModelAppearance Look = Hero->GetAppearance();
//   const int32 Armor = Hero->GetSource()->FindMaterial( "Armor" );
//   FModelMaterialColors Colors = Hero->GetSource()->GetStructure().Materials[Armor].Colors;
//   Colors.Emissive = FColor( 0.6f, 0.1f, 0.1f, 1.0f );   // 赤く光らせる（被弾など）
//   Look.SetMaterialColors( Armor, Colors );
//   ...
//   Look.ResetMaterialColors( Armor );                      // 読み込んだときの色へ戻す
//=============================================================================
class FModelAppearance
{
public:
	FModelAppearance( int32 InHandle, const CModel* InSource );

	// 材質の色（いまの値）。読めなければ false で、Out は変えない
	bool GetMaterialColors( int32 Material, FModelMaterialColors& Out ) const;
	bool SetMaterialColors( int32 Material, const FModelMaterialColors& Colors );
	// 光が当たった所の色だけを替える（ほかの色はそのまま）
	bool SetMaterialDiffuse( int32 Material, const FColor& Diffuse );
	// 読み込んだときの色へ戻す（Material が -1 なら、すべての材質）
	bool ResetMaterialColors( int32 Material = -1 );

	// メッシュ・フレームを描くか（フレームを隠すと、その子のフレームのメッシュも描かない）。分からないときは true
	bool IsMeshVisible( int32 Mesh ) const;
	bool SetMeshVisible( int32 Mesh, bool bVisible );
	bool IsFrameVisible( int32 Frame ) const;
	bool SetFrameVisible( int32 Frame, bool bVisible );

	// 形状（モーフ）の効き具合（0 = 効かない、1 = すべて効く。番号は CModel::GetStructure().Shapes の並び、名前からは CModel::FindShape）。分からないときは 0
	float32 GetShapeRate( int32 Shape ) const;
	bool SetShapeRate( int32 Shape, float32 Rate );

	// 全体の不透明度（0〜1 に収める）。分からないときは 1
	float32 GetOpacity() const;
	bool SetOpacity( float32 Opacity );

private:
	int32 Handle;
	const CModel* Source;
};
