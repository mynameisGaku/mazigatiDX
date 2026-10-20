#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// アウトライナー（オブジェクトの一覧の木）の表示内容
//   画面に出す行の並びを作る純粋な関数（ImGui を使わない）。ImGui 側は、この行を順に出すだけ。
//
//   絞り込み（Filter）: 空白で区切った語を、すべて満たすものだけ（AND）。
//     tag:Enemy   タグ Enemy を持つ（大文字小文字を区別する。コードの文字列と同じ綴りで探すため）
//     comp:Model  部品 Model（Billboard / Sound / Collider、知らない種類はその名前）を持つ
//     #12         Id が 12
//     torch       名前に torch を含む（大文字小文字を区別しない）
//   絞り込み中は、一致したものと、その祖先（道筋）を出して、畳みの状態は無視して開く。
//=============================================================================
struct FEditorOutlinerRow
{
	int32 ObjectId = WorldNoId;
	int32 Depth = 0;
	bool bHasChildren = false;   // 子がある（絞り込み中でも、全体の木での話）
	bool bExpanded = false;      // 子を出している
	bool bMatch = true;          // 絞り込みに一致した（道筋として出しているだけの祖先は false。絞り込みが無ければ true）
};

namespace NWorldEditor
{
	// 1 つの語が、オブジェクトに一致するか
	bool MatchesFilterTerm( const FWorldObject& Object, const FString& Term );
	// 空白で区切った語のすべてに一致するか（空の Filter はすべて一致）
	bool MatchesFilter( const FWorldObject& Object, const FString& Filter );

	// 行の並び（親が先の深さ優先。兄弟は配列の順）。Collapsed に入っている Id の子は出さない。
	// 存在しない親を持つオブジェクトは最上位として、循環していて木からたどれないものは、最後に最上位として出す（見つけて消せるように）
	TArray<FEditorOutlinerRow> BuildOutlinerRows( const FWorldDefinition& World, const TSet<int32>& Collapsed, const FString& Filter );
}
