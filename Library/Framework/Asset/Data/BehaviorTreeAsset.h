// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeDefinition.h"
#include "Library/Framework/Asset/Asset.h"

//=============================================================================
// CBehaviorTreeAsset
//   読み込んだビヘイビアツリーの定義（.gxbt）。CAssetSubsystem::LoadBehaviorTree で読み込む。
//   中身は FBehaviorTreeDefinition（純粋なデータ。書式は BehaviorTreeDefinition.h の先頭）。
//   部品の名前・ブラックボードのキーの解決は、実行用のインスタンス（CBehaviorTree::Create）を作るときに行う。
//
//   TSharedPtr<CBehaviorTreeAsset> Asset = Assets->LoadBehaviorTree( "AI/Enemy.gxbt" );
//   TUniquePtr<CBehaviorTree> Tree = CBehaviorTree::Create( Asset->GetDefinitionShared(), NAI::GetRegistry(), Blackboard.GetDefinition(), &Issues );
//
//   定義を作り直して保存するときは Assets->SaveBehaviorTree( Path, Definition )（保存したパスのキャッシュは捨てる）。
//=============================================================================
class CBehaviorTreeAsset : public CAsset
{
public:
	CBehaviorTreeAsset( const FString& InPath, const FBehaviorTreeDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues );

	FORCEINLINE const FBehaviorTreeDefinition& GetDefinition() const { return *Definition; }
	// 実行用のインスタンスが参照し続けるので、作るときには、こちらを渡す
	FORCEINLINE const TSharedPtr<const FBehaviorTreeDefinition>& GetDefinitionShared() const { return Definition; }
	// 読み込みのとき、捨てた項目などの問題
	FORCEINLINE const TArray<FAIIssue>& GetLoadIssues() const { return LoadIssues; }

	FString Describe() const override;
	int64 GetMemoryBytes() const override;

private:
	TSharedPtr<const FBehaviorTreeDefinition> Definition;
	TArray<FAIIssue> LoadIssues;
};
