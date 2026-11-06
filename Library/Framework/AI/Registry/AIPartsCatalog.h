// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Core/Collection/String.h"
#include "Library/Json/JsonObject.h"

class CAssetSubsystem;

//=============================================================================
// 部品のカタログ（.gxparts）
//   ゲームが C++ で登録した AI の部品（Task / Condition / Service / ステートの処理）の、名前・カテゴリ・説明・パラメータのスキーマ・別名を、
//   JSON のファイルにする。エディタ専用の実行ファイル（Editor.exe）は、ゲームの C++ を持たないので、このカタログを読んで、
//   「名前とスキーマだけの部品」（何もしない）を登録する。これで、エディタは、ゲームの部品を一覧から選ばせ、パラメータの入力欄をスキーマから作り、
//   未登録の警告を出さなくなる（実行は、ゲームの実行ファイルが、本物の部品で行う）。
//
//   ゲーム側: NAI::ExportPartsCatalog( NAI::GetRegistry() ) を書き出す（コンソールの ai.parts、起動引数 --export-ai-parts）。置き場所は、アセットのルートの AI/AIParts.gxparts
//   エディタ側: AI エディタが、起動時と File > Reload parts catalog で、このファイルを読む（NAI::ImportPartsCatalog）
//
//   {
//     "Version": 1,
//     "Tasks": [ { "Name": "Wait", "Category": "Flow", "Description": "...", "Params": [ { "Name": "Seconds", "Type": "Float", "Default": 1.0, "Min": 0, "Max": 3600, "Hint": "..." } ] } ],
//     "StateActions": [ ... ], "Conditions": [ ... ], "Services": [ ... ],
//     "Aliases": [ { "Old": "OldName", "Name": "NewName" } ]
//   }
//=============================================================================
namespace NAI
{
	// レジストリの全部品と別名を、カタログの JSON にする
	NlohmannJson ExportPartsCatalog( const CAIRegistry& Registry );

	// カタログの部品を、名前とスキーマだけの部品（何もしない）として登録する。すでに登録されている名前は、そのまま残す（本物の部品を上書きしない）。
	// 登録した部品の数を返す。形が違う項目は飛ばして、Issues に足す
	int32 ImportPartsCatalog( CAIRegistry& Registry, const NlohmannJson& Catalog, TArray<FAIIssue>* Issues = nullptr );

	// アセットのルートからの相対パス（既定は DefaultPartsCatalogPath）へ、NAI::GetRegistry() の部品のカタログを書き出す。書けたら true
	bool SavePartsCatalog( CAssetSubsystem& Assets, const FString& RelativePath = FString() );
	// そのファイルを読んで、レジストリへ取り込む（ImportPartsCatalog）。ファイルが無い・読めないなら -1、そうでなければ登録した部品の数
	int32 LoadPartsCatalog( CAssetSubsystem& Assets, CAIRegistry& Registry, const FString& RelativePath = FString(), TArray<FAIIssue>* Issues = nullptr );

	// カタログの、アセットのルートからの既定の場所
	constexpr const char* DefaultPartsCatalogPath = "AI/AIParts.gxparts";
}
