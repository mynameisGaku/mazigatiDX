// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/StateMachine/StateMachineDefinition.h"
#include "Library/Framework/Asset/Definition/DefinitionDocument.h"

//=============================================================================
// ステートマシン定義の編集（エディタの画面から独立した、編集の操作と履歴）
//   エディタ（ImGui）に依存しない。画面は、ここの関数を呼ぶだけにして、「どう編集されるか」（名前の重複・参照のずれの補正・元に戻す）はここでテストする。
//   すべての関数は、Document.BeginEdit() で履歴に積んでから書き換える（範囲外の番号・変わらない操作は、何もしないで、履歴にも積まない）。
//   連続した変更は、CoalesceKey に同じ文字列を渡し続けると、1 回の編集にまとまる。
//
//   ステートの番号は、FStateMachineDefinition::States の番号（親の前に子が来る順）。Machine は、遷移を持つマシンのステートの番号（-1 は最上位）。
//   名前を直接参照している所（親の Entry・遷移の From / To（"A/B" の経路を含む））は、ステートを改名・削除したときに、ここで直す。
//=============================================================================
using TStateMachineDocument = TDefinitionDocument<FStateMachineDefinition>;

// ステートの処理の一覧
enum class EStateActionList : uint8
{
	OnEnter,
	OnTick,
	OnExit,
};

namespace NStateMachineEdit
{
	void SetBlackboard( TStateMachineDocument& Document, const FString& Path );

	// マシンの、最初に入る子（Machine が -1 なら最上位）。Name は子の名前（"A/B" の経路でもよい）
	void SetEntry( TStateMachineDocument& Document, int32 Machine, const FString& Name );

	// ステートを足す。Parent が葉なら、Machine にする。名前は、同じ親の中で重複しないようにする（"/" は "_" にする）。
	// 親の Entry が空なら、足したステートを Entry にする。足したステートの番号を返す（Parent が範囲外なら INDEX_NONE）
	int32 AddState( TStateMachineDocument& Document, int32 Parent, const FString& Name, bool bMachine = false );

	// ステートを、子孫ごと消す。それを指す遷移も消し、Entry が指していたら、残った最初の子へ直す
	void RemoveState( TStateMachineDocument& Document, int32 StateIndex );

	// 改名する（重複・空・"/" は直す）。実際の名前を返す。Entry・遷移の From / To は、新しい名前に直す
	FString RenameState( TStateMachineDocument& Document, int32 StateIndex, const FString& Name );

	// Machine にする / 葉へ戻す（子が残っているマシンは、葉へ戻せない）
	void SetStateIsMachine( TStateMachineDocument& Document, int32 StateIndex, bool bMachine );

	void SetStateClip( TStateMachineDocument& Document, int32 StateIndex, const FString& Clip, const char* CoalesceKey = nullptr );

	// エディタの座標（State.Editor の X / Y。実行側は読まない）
	void SetStatePosition( TStateMachineDocument& Document, int32 StateIndex, float32 X, float32 Y, const char* CoalesceKey = nullptr );

	// 遷移。bAny は、Any State の一覧（From は無視される）。足した遷移の、その一覧の中の番号を返す（Machine が範囲外・葉なら INDEX_NONE）
	int32 AddTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, const FString& From, const FString& To );
	void RemoveTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 Index );
	void MoveTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 From, int32 To );
	void SetTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 Index, const FStateTransitionDef& Transition, const char* CoalesceKey = nullptr );

	// ステートの処理（OnEnter / OnTick / OnExit）
	int32 AddAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, const FAICall& Call );
	void RemoveAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 Index );
	void MoveAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 From, int32 To );
	void SetAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 Index, const FAICall& Call, const char* CoalesceKey = nullptr );

	// アニメーションの出来事に反応する処理
	int32 AddClipEvent( TStateMachineDocument& Document, int32 StateIndex, const FStateClipEventDef& Event );
	void RemoveClipEvent( TStateMachineDocument& Document, int32 StateIndex, int32 Index );
	void SetClipEvent( TStateMachineDocument& Document, int32 StateIndex, int32 Index, const FStateClipEventDef& Event, const char* CoalesceKey = nullptr );

	// ブラックボードのキーを使っている条件の数（遷移の BlackboardCompare。部品のパラメータの中は数えない）
	int32 CountBlackboardKeyUsage( const FStateMachineDefinition& Definition, const FString& Key );
	// ブラックボードのキーの改名に合わせて、条件の参照を直す。直した数を返す（0 なら、履歴に積まない）
	int32 RenameBlackboardKey( TStateMachineDocument& Document, const FString& OldName, const FString& NewName );
}
