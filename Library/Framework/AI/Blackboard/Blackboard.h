// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"

//=============================================================================
// CBlackboard
//   AI が参照する値の置き場（実行時のインスタンス）。センサー（視界・入力）が書き、条件（遷移・デコレーター）が読み、部品が書き換える。
//   定義（FBlackboardDefinition）は共有し、値だけをインスタンスごとに持つ。
//
//   TSharedPtr<const FBlackboardDefinition> Definition = ...;
//   CBlackboard Blackboard( Definition );
//   const FBlackboardKey HasTarget = Blackboard.FindKey( "HasTarget" );     // 名前は 1 回だけ引いて、番号を使う
//   Blackboard.SetBool( HasTarget, true );
//   if ( Blackboard.GetBool( HasTarget ) ) { ... }
//
//   - 型違い・範囲外のキーの読み書きは、既定値を返す / 何もしない（警告は同じキーにつき 1 回だけ）
//   - 値が実際に変わったときだけ、そのキーのリビジョン（GetRevision）と全体のリビジョン（GetChangeCounter）が増える。
//     同じ値の書き込みは変更として数えない（ビヘイビアツリーのデコレーターが、変わったキーだけを再評価するため）
//   - Object のキーは、ポインターではなく CGameObject::GetId() の値で持つ。使うときに CScene::FindObjectById で引く
//   - Trigger のキー（1 フレームだけ真）は、ClearTriggers() で偽へ戻る。フレームの終わりに呼ぶ（CStateMachineComponent などが呼ぶ）
//   - メインスレッドで使う
//=============================================================================
class CBlackboard
{
public:
	explicit CBlackboard( TSharedPtr<const FBlackboardDefinition> InDefinition );

	const FBlackboardDefinition& GetDefinition() const { return *Definition; }
	const TSharedPtr<const FBlackboardDefinition>& GetDefinitionPtr() const { return Definition; }
	int32 GetNumKeys() const { return Definition->Keys.Num(); }

	// 名前のキー。無ければ無効なキー（IsValid が false）。ログは出さない
	FBlackboardKey FindKey( const FString& Name ) const;
	// キーが有効か（範囲内か）
	bool IsValidKey( FBlackboardKey Key ) const;
	EBlackboardKeyType GetKeyType( FBlackboardKey Key ) const;
	const FString& GetKeyName( FBlackboardKey Key ) const;

	//-------------------------------------------------------------------------
	// 読む（型違い・範囲外は既定値）
	//-------------------------------------------------------------------------
	bool GetBool( FBlackboardKey Key ) const;
	int32 GetInt( FBlackboardKey Key ) const;
	float32 GetFloat( FBlackboardKey Key ) const;
	FVector3 GetVector( FBlackboardKey Key ) const;
	const FString& GetName( FBlackboardKey Key ) const;
	uint64 GetObjectId( FBlackboardKey Key ) const;
	// Enum は選択肢の番号。名前は GetEnumName
	int32 GetEnum( FBlackboardKey Key ) const;
	FString GetEnumName( FBlackboardKey Key ) const;

	//-------------------------------------------------------------------------
	// 書く（値が変わったら true。型違い・範囲外・NaN・範囲外の選択肢は何もせず false）
	//-------------------------------------------------------------------------
	bool SetBool( FBlackboardKey Key, bool bValue );
	bool SetInt( FBlackboardKey Key, int32 Value );
	bool SetFloat( FBlackboardKey Key, float32 Value );
	bool SetVector( FBlackboardKey Key, const FVector3& Value );
	bool SetName( FBlackboardKey Key, const FString& Value );
	bool SetObjectId( FBlackboardKey Key, uint64 Id );
	bool SetEnum( FBlackboardKey Key, int32 Index );
	// 選択肢の名前で書く。無い名前は何もせず false
	bool SetEnumByName( FBlackboardKey Key, const FString& Name );

	// 型を問わずに読む / 書く（デバッガ・エディタ・汎用の部品用）。書くときは、型に合わない値（NaN など）を弾く
	bool GetValue( FBlackboardKey Key, FBlackboardValue& OutValue ) const;
	bool SetValue( FBlackboardKey Key, const FBlackboardValue& Value );

	//-------------------------------------------------------------------------
	// 変更の追跡
	//-------------------------------------------------------------------------
	// キーのリビジョン（書き込みで値が変わるたびに増える）。無効なキーは 0
	uint32 GetRevision( FBlackboardKey Key ) const;
	// どのキーでも、値が変わるたびに増える
	uint32 GetChangeCounter() const { return ChangeCounter; }

	//-------------------------------------------------------------------------
	// 初期化
	//-------------------------------------------------------------------------
	// すべてのキーを既定値へ戻す（リビジョンは進める）
	void ResetToDefaults();
	// Trigger のキーを偽へ戻す。フレームの終わりに呼ぶ
	void ClearTriggers();
	// 別のブラックボード（定義が違ってよい）から、名前と型が同じキーの値を引き継ぐ。引き継いだ数を返す（ホットリロード用）
	int32 CopyMatchingValuesFrom( const CBlackboard& Other );

private:
	// 型と範囲を確かめる。だめなら警告（同じキーにつき 1 回だけ）して false
	bool CheckKey( FBlackboardKey Key, EBlackboardKeyType Expected, const char* Operation ) const;
	void WarnOnce( int32 WarnKey, const FString& Message ) const;
	bool Store( int32 Index, const FBlackboardValue& Value );

	TSharedPtr<const FBlackboardDefinition> Definition;
	TArray<FBlackboardValue> Values;
	TArray<uint32> Revisions;
	uint32 ChangeCounter = 0;
	// 警告を出したキー（-1 は「無効なキー」の警告）
	mutable TSet<int32> Warned;
};
