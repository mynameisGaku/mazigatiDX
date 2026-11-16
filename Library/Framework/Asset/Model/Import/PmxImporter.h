// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Model/Import/FbxImporter.h"
#include "Library/Framework/Asset/Model/Import/ImportedScene.h"

//=============================================================================
// PMX（MikuMikuDance のモデル）の取り込み
//
//   PMX 2.0 / 2.1 の、頂点・面・画像・材質・骨を読み、FImportedScene にする（骨組みをCPUで変形して描くための、骨とスキンつき）。
//   ・座標系: PMX は、DxLib と同じ左手系（Y が上）。座標・向きは、そのまま使い、FImportedScene::bLeftHanded を true にする
//     （NImportedScene::ConvertToLeftHanded は、何もしない）
//   ・骨: PMX の骨の位置から、「親から見た移動」だけの階層にする（PMX の骨には、向きが無い）。骨の番号は PMX と同じ
//   ・スキン: BDEF1 / BDEF2 / BDEF4 / SDEF（BDEF2 として扱う）/ QDEF（BDEF4 として扱う）。骨ごとのクラスターは、移動だけのバインド行列
//   ・材質: 拡散色・鏡面・鏡面の強さ・画像（PMX の画像の番号）。三角形は、材質の順に並んでいる
//   ・読まないもの: モーフ・表示枠・剛体・ジョイント・IK・付与（PMX のモーションは、別のファイル（VMD））
//=============================================================================
namespace NModelImport
{
	// PMX のファイルの中身から、OutScene を作る。読めなければ false（OutError に理由）。Options は、スキンの影響数（MaxSkinInfluences）だけを使う
	bool ImportPmx( const void* Data, usize Size, const FImportOptions& Options, FImportedScene& OutScene, FString& OutError );
}
