// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Model/Import/ModelImporter.h"
#include "Library/Framework/Asset/Model/Model.h"

//=============================================================================
// CModelLoader
//   3D モデル（アニメーションだけのファイルも同じ）を読み込む人。どのファイルを実際に読むか（.fbx / .obj は .mv1 に変換したもの、など）は
//   CModelImporter が決め、この人はそれを読んで、同じファイルなら共有する。
//   使うときは、CAssetSubsystem::LoadModel から呼ばれる。
//=============================================================================
class CModelLoader
{
public:
	CModelLoader( CAssetLoadContext& InContext, const CModelImporter& InImporter );

	// モデルのファイルから。bAsync なら、読み込みの完了を待たずに返す（IsReady が true になるまで使わない）。読めなければ nullptr
	TSharedPtr<CModel> Load( const FString& Path, bool bAsync );

	// 覚えているモデル（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	const CModelImporter& Importer;
	TAssetCache<CModel> Cache;
};
