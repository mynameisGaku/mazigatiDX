// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/Blackboard/BlackboardCompare.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Framework/AI/Registry/ParamSchema.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// スキーマ駆動のインスペクタ（AI エディタの入力欄）
//   登録した部品のパラメータの定義（FParamSchema。名前・型・既定値・範囲・説明）から、入力欄を自動で作る。
//   部品ごとの画面のコードは書かない（新しい部品を登録すれば、その入力欄が出る）。
//
//   - Bool: チェック / Int・Float: スライダー（範囲があるとき）か数値入力 / String・AssetPath: 文字入力
//   - Enum: 選択肢の一覧 / BlackboardKey: ブラックボードのキーの一覧（無ければ文字入力）/ Clip: クリップ名の一覧（無ければ文字入力）
//   - Scalar（型が実行時に決まる値）: JSON として入力（Enter で反映）
//   - 説明（Hint）は、項目にマウスを置くと出る。必須は * を付ける。既定値と同じ項目は、保存する JSON に書かない
//=============================================================================
struct FAIInspectContext
{
	// BlackboardKey の選択肢にするブラックボード（nullptr なら、キーの名前を文字で入力する）
	const FBlackboardDefinition* Blackboard = nullptr;
	// Clip の選択肢（nullptr か空なら、クリップ名を文字で入力する）
	const TArray<FString>* ClipNames = nullptr;
};

namespace NDebugUI
{
	// Params（部品の呼び出しの "Params"。オブジェクトか null）を、Schema の入力欄で編集する。変更されたら true（Params は、既定値と同じ項目を省いた形に直る）
	bool DrawParamInspector( const FParamSchema& Schema, NlohmannJson& Params, const FAIInspectContext& Context );

	// ブラックボードのキーを選ぶ一覧（Context にブラックボードが無ければ文字入力）。キーが定義に無いときは、赤で出す。変更されたら true
	bool DrawBlackboardKeyCombo( const char* Label, FString& Key, const FAIInspectContext& Context );

	// 比較の演算子の選択（Key の型が大小を比べられなければ、== と != だけ。Key が nullptr なら全部）。変更されたら true
	bool DrawCompareOpCombo( const char* Label, EAICompareOp& Op, const FBlackboardKeyDefinition* Key );

	// ブラックボードの値（JSON）の入力欄。Key の型に合わせる（Key が nullptr なら、JSON のまま文字で入力。Enter で反映）。変更されたら true
	bool DrawBlackboardValueInput( const char* Label, const FBlackboardKeyDefinition* Key, NlohmannJson& Value );

	// 文字列を選ぶ一覧（絞り込みつき。Items に無い値は、赤で出す）。変更されたら true
	bool DrawNameCombo( const char* Label, FString& Value, const TArray<FString>& Items, const char* NoneText = "(none)" );
}
