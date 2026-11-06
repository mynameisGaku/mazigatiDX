// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/StateMachine/StateMachineDefinition.h"
#include "Library/Framework/Asset/Asset.h"

//=============================================================================
// CStateMachineAsset
//   読み込んだステートマシンの定義（.gxstate）。CAssetSubsystem::LoadStateMachine で読み込む。
//   中身は FStateMachineDefinition（純粋なデータ。書式は StateMachineDefinition.h の先頭）。
//   部品の名前・ブラックボードのキーの解決は、実行用のインスタンス（CStateMachine::Create）を作るときに行う。
//
//   TSharedPtr<CStateMachineAsset> Asset = Assets->LoadStateMachine( "AI/Player.gxstate" );
//   TUniquePtr<CStateMachine> Machine = CStateMachine::Create( Asset->GetDefinitionShared(), NAI::GetRegistry(), Blackboard.GetDefinition(), &Issues );
//
//   定義を作り直して保存するときは Assets->SaveStateMachine( Path, Definition )（保存したパスのキャッシュは捨てる）。
//=============================================================================
class CStateMachineAsset : public CAsset
{
public:
	CStateMachineAsset( const FString& InPath, const FStateMachineDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues );

	FORCEINLINE const FStateMachineDefinition& GetDefinition() const { return *Definition; }
	// 実行用のインスタンスが参照し続けるので、作るときには、こちらを渡す
	FORCEINLINE const TSharedPtr<const FStateMachineDefinition>& GetDefinitionShared() const { return Definition; }
	// 読み込みのとき、捨てた項目などの問題
	FORCEINLINE const TArray<FAIIssue>& GetLoadIssues() const { return LoadIssues; }

	FString Describe() const override;
	int64 GetMemoryBytes() const override;

private:
	TSharedPtr<const FStateMachineDefinition> Definition;
	TArray<FAIIssue> LoadIssues;
};
