// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Model/Import/ModelExporter.h"
#include "Library/Framework/Asset/Model/ModelAnimationAsset.h"

//=============================================================================
// CModelImporter
//   モデルのファイルを取り込み、.mv1 に出力する人。.fbx / .obj のような、バックエンドが直接読めない形式を .mv1 に変換し、
//   モデルを読み込むときに「実際にどのファイルを読むか」（元のファイルか、隣の最新の .mv1 か）を決める。
//   モデルを読み込むこと自体は、CAssetSubsystem::LoadModel が行う。使うときは CAssetSubsystem::ExportModelAsMV1 などから呼ばれる。
//
//   - 出力が新しければ何もしない（Options.bForce で作り直す）
//   - 自動の取り込み（既定 true）が off で、.mv1 が無い・古いときは、あるものを（古くても警告つきで）読む
//=============================================================================
class CModelImporter
{
public:
	explicit CModelImporter( CAssetLoadContext& InContext );

	//-------------------------------------------------------------------------
	// モデルを読み込むときの下ごしらえ
	//-------------------------------------------------------------------------
	// ユーザーが指したファイルの絶対パス（キャッシュのキー）を決める。元の .fbx が無くて変換済みの .mv1 だけがある（配布版）ときは、.mv1 を指す。
	// 見つからなければ false（ログに出す）
	bool ResolveRequest( const FString& Path, FString& OutFullPath ) const;

	// 実際に読ませるファイルを決める。.fbx / .obj は、最新の .mv1 があればそれ、無ければ自動で作る。
	// バックエンドが読める形式でも、最新の .mv1 が隣にあれば、そちらを読む（SetPreferConvertedModels）。決められなければ false
	bool ResolveLoadPath( const FString& FullPath, FString& OutLoadPath ) const;

	//-------------------------------------------------------------------------
	// .mv1 への出力
	//-------------------------------------------------------------------------
	// 元のファイルを .mv1 にして保存する（既定は同じフォルダ・同じ名前）。出力が新しければ何もしない（Options.bForce で作り直す）。
	// メッシュの無いファイル（モーションだけ）は、アニメーションだけの .mv1 になる。失敗の理由は結果とログに出る
	FModelExportResult ExportAsMV1( const FString& SourcePath, const FModelExportOptions& Options = FModelExportOptions() ) const;

	// ルートフォルダの下の、.mv1 にできるファイルと、出力が最新かどうかの一覧
	TArray<FModelSourceInfo> ScanSources() const;

	// 一覧のうち、.mv1 が無い・古いもの（bForce なら全部）を .mv1 にする
	FModelBatchExportResult ExportAll( bool bForce = false, const FModelExportOptions& Options = FModelExportOptions() ) const;

	// 定義が使っているモデルのファイル（Model / PreviewModel / 各クリップの Source）のうち、.mv1 にできるものを .mv1 にする
	FModelBatchExportResult ExportUsedBy( const FModelAnimationDefinition& Definition, bool bForce = false ) const;

	//-------------------------------------------------------------------------
	// 設定
	//-------------------------------------------------------------------------
	// 読み込むときに、.fbx / .obj の .mv1 が無い・古いときに、自動で作る（既定 true）
	void SetAutoImport( bool bEnable )
	{
		bAutoImport = bEnable;
	}

	bool GetAutoImport() const
	{
		return bAutoImport;
	}

	// 読み込むときに、バックエンドが読める形式（.x / .pmx など）にも、最新の .mv1 が隣にあれば、そちらを読む（既定 true）
	void SetPreferConverted( bool bEnable )
	{
		bPreferConverted = bEnable;
	}

	bool GetPreferConverted() const
	{
		return bPreferConverted;
	}

private:
	CAssetLoadContext& Context;
	bool bAutoImport = true;
	bool bPreferConverted = true;
};
