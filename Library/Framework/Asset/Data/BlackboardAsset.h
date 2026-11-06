// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Framework/Asset/Asset.h"

//=============================================================================
// CBlackboardAsset
//   読み込んだブラックボードの定義（.gxblackboard）。CAssetSubsystem::LoadBlackboard で読み込む。
//   中身は FBlackboardDefinition（純粋なデータ。書式は BlackboardDefinition.h の先頭）。
//
//   TSharedPtr<CBlackboardAsset> Asset = Assets->LoadBlackboard( "AI/Player.gxblackboard" );
//   CBlackboard Blackboard( Asset->GetDefinitionShared() );
//
//   定義を作り直して保存するときは Assets->SaveBlackboard( Path, Definition )（保存したパスのキャッシュは捨てる）。
//=============================================================================
class CBlackboardAsset : public CAsset
{
public:
	CBlackboardAsset( const FString& InPath, const FBlackboardDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues );

	FORCEINLINE const FBlackboardDefinition& GetDefinition() const { return *Definition; }
	// ブラックボードのインスタンスが参照し続けるので、作るときには、こちらを渡す
	FORCEINLINE const TSharedPtr<const FBlackboardDefinition>& GetDefinitionShared() const { return Definition; }
	// 読み込みのとき、捨てたキーなどの問題
	FORCEINLINE const TArray<FAIIssue>& GetLoadIssues() const { return LoadIssues; }

	FString Describe() const override;
	int64 GetMemoryBytes() const override;

private:
	TSharedPtr<const FBlackboardDefinition> Definition;
	TArray<FAIIssue> LoadIssues;
};
