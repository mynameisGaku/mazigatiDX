// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Draw/DrawRibbon.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

// DxLibの帯頂点と三角形番号を所有する、一回の展開結果。
struct FDxLibRibbonMesh
{
	// 幅方向へ展開した頂点列。texture split時は反復ごとに複製される。
	TArray<VERTEX3D> Vertices;
	// 三角形listとして頂点を結ぶ16bit番号列。
	TArray<unsigned short> Indices;
};

// 旧Ribbonと材質Ribbonで同じmiter・ghost・UV展開を使う内部変換関数。
namespace NDxLibRibbonMesh
{
	// ribbon入力からmeshを作る。bSplitTextureがtrueなら旧描画用に整数U境界で分ける。
	bool Build( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, bool bSplitTexture, FDxLibRibbonMesh& Mesh );
}
