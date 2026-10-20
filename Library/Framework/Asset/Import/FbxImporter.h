#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Import/ImportedScene.h"

//=============================================================================
// FBX / OBJ の取り込み（ufbx を使う。VendorModules/ufbx）
//
//   FImportedScene Scene;
//   FString Error;
//   if ( !NModelImport::ImportFromMemory( Bytes.data(), Bytes.size(), "C:/Assets/Robot.fbx", FImportOptions(), Scene, Error ) ) ...
//
//   読んだ結果は、右手系（Y が上）の FImportedScene。FBX の単位（cm など）・軸は、そのまま（Y が上の右手系に回すだけ）。
//   ・ノード: ピボット・ジオメトリ変換・継承モードを展開して、単純な「移動・回転・拡大だけ」の階層にする
//   ・メッシュ: 三角形にして、座標・法線・UV の組ごとに頂点を分ける。スキンは 1 頂点に最大 4 本の骨
//   ・アニメーション: 動くノードだけ、毎秒 AnimFps コマで行列にサンプリングする（キーの番号 = コマ）
//   ・マテリアル: 拡散色・鏡面・自己発光・拡散色の画像（ファイル名 または埋め込み）
//=============================================================================
struct FImportOptions
{
	float32 AnimFps = 30.0f;           // アニメーションをサンプリングする、毎秒のコマ数
	bool bImportAnimations = true;
};

namespace NModelImport
{
	// Data / Size: ファイルの中身。FileName: 元のファイルのフルパス（形式の判定と、OBJ が参照する .mtl を探す場所に使う）。
	// 読めなければ false（OutError に理由）
	bool ImportFromMemory( const void* Data, usize Size, const FString& FileName, const FImportOptions& Options, FImportedScene& OutScene, FString& OutError );
}
