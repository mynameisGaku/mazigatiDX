// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Framework/Asset/Definition/DefinitionDocument.h"

//=============================================================================
// ブラックボード定義の編集（エディタの画面から独立した、編集の操作と履歴）
//   エディタ（ImGui）に依存しない。画面は、ここの関数を呼ぶだけにして、「どう編集されるか」（名前の重複・型を変えたときの後始末・元に戻す）はここでテストする。
//   すべての関数は、Document.BeginEdit() で履歴に積んでから書き換える（範囲外の番号・変わらない操作は、何もしないで、履歴にも積まない）。
//   連続した変更（スライダーのドラッグなど）は、CoalesceKey に同じ文字列を渡し続けると、1 回の編集にまとまる。
//=============================================================================
using TBlackboardDocument = TDefinitionDocument<FBlackboardDefinition>;

namespace NBlackboardEdit
{
	// キーを足す（型の初期値で作る）。名前は、重複しないように必要なら "Name 2" にする。足したキーの番号を返す
	int32 AddKey( TBlackboardDocument& Document, const FString& Name, EBlackboardKeyType Type );

	void RemoveKey( TBlackboardDocument& Document, int32 KeyIndex );

	// 改名する。重複・空の名前は直す。実際の名前を返す。ほかの定義（ステートマシン・ビヘイビアツリー）の参照は、それぞれの RenameBlackboardKey で直す
	FString RenameKey( TBlackboardDocument& Document, int32 KeyIndex, const FString& Name );

	// 並べ替える（From の位置のキーを To の位置へ）
	void MoveKey( TBlackboardDocument& Document, int32 From, int32 To );

	// 型を変える。既定値は型の初期値に戻り、Enum の選択肢は Enum 以外で消え、Trigger は Bool 以外で偽になる
	void SetKeyType( TBlackboardDocument& Document, int32 KeyIndex, EBlackboardKeyType Type );

	// 1 フレームだけ真になる Bool か（Bool 以外のキーには効かない）
	void SetKeyTrigger( TBlackboardDocument& Document, int32 KeyIndex, bool bTrigger );

	void SetKeyDefault( TBlackboardDocument& Document, int32 KeyIndex, const FBlackboardValue& Value, const char* CoalesceKey = nullptr );

	// Enum の選択肢（Enum 以外のキーには効かない）。足した選択肢の番号を返す（足せなければ INDEX_NONE）
	int32 AddEnumValue( TBlackboardDocument& Document, int32 KeyIndex, const FString& Name );
	// 消す。既定値が消した選択肢なら先頭に、あとの選択肢なら番号を 1 つ詰める
	void RemoveEnumValue( TBlackboardDocument& Document, int32 KeyIndex, int32 ValueIndex );
	// 改名する（重複は直す）。実際の名前を返す。条件の値（文字列）の参照は直さない
	FString RenameEnumValue( TBlackboardDocument& Document, int32 KeyIndex, int32 ValueIndex, const FString& Name );
}
