// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeDefinition.h"
#include "Library/Framework/AI/StateMachine/StateMachineRuntime.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// AI のグラフの配置と、矢印の経路の計算
//   ステートマシンとビヘイビアツリーの定義から、箱の位置・大きさと、矢印の経路（直角に折れる線の点）と、中継ノードの位置を求める。
//   ImGui の描画は呼ばない（ImVec2 だけを使う）ので、AI デバッガの表示（AIGraphViews）も、将来のエディタも、同じ結果を使える
//   （エディタは、この配置の上でノードを動かす・矢印をつなぐ操作を重ねる）。座標は、FNodeGraphCanvas のワールド座標。
//
//   ステートマシンの配置: 入れ子のマシンは、親の箱の中に子を並べる。子は、入口からの遷移をたどった順（幅優先）に、左から右の列へ並べる。
//     Any State 遷移は、そのマシンの左の「ANY」の箱から出る。矢印は、直角に折れる線にする:
//       前（右）へ         横 -> 縦 -> 横。同じ箱から複数の矢印が出る（入る）ときは、その分かれ目（合わさる所）に中継ノードを置く
//       戻る（左）へ       箱の下の通路を回る（途中に中継ノード。ラベルもここに付く）
//       Any State          箱の上の通路を回る（途中に中継ノード）
//       同じ列の上下へ     縦 -> 横 -> 縦
//       自分へ戻る         箱の上で小さく回る
//   ビヘイビアツリーの配置: 葉を左から並べ、親は子の真ん中へ。親から子へは 縦 -> 横 -> 縦（子が 2 つ以上なら、分かれ目に中継ノード）。
//=============================================================================
namespace NAIGraph
{
	// 箱の寸法（ワールド座標）
	constexpr float LeafWidth = 156.0f;
	constexpr float LeafHeight = 50.0f;
	constexpr float ColumnGap = 70.0f;
	constexpr float RowGap = 26.0f;
	constexpr float Padding = 18.0f;
	constexpr float HeaderHeight = 26.0f;
	constexpr float AnyWidth = 64.0f;
	constexpr float AnyHeight = 36.0f;
	// 矢印を通す、箱の上下の通路の高さ
	constexpr float Lane = 56.0f;
	// 通路が複数あるときの、通路どうしの間隔（中継ノードが入る広さ）
	constexpr float LaneStep = 17.0f;

	constexpr float TreeNodeWidth = 218.0f;
	constexpr float TreeGapX = 26.0f;
	constexpr float TreeGapY = 46.0f;
	constexpr float TreeLineHeight = 15.0f;
	constexpr float TreeHeaderHeight = 40.0f;
}

// ステートマシンの、1 つのマシン（最上位か、入れ子の Machine）の配置
struct FStateGraphMachine
{
	// 子（定義のステートの番号。定義の順）
	TArray<int32> Children;
	// 最初に入る子（ステートの番号。無ければ -1）
	int32 EntryChild = -1;
	// Any State 遷移を持つか。持つなら、AnyPos（左上）に「ANY」の箱を置く（大きさは NAIGraph::AnyWidth x AnyHeight）
	bool bHasAny = false;
	ImVec2 AnyPos = ImVec2( 0.0f, 0.0f );
	// 中身の範囲（左上と大きさ。上下の通路を含む）
	ImVec2 Origin = ImVec2( 0.0f, 0.0f );
	ImVec2 Content = ImVec2( 0.0f, 0.0f );
};

// 遷移 1 つの矢印の経路
struct FStateGraphRoute
{
	// FStateTransitionInfo の Id と、持ち主のマシン、元の子（-1 は ANY）、先の子（持ち主のマシンの直接の子）
	int32 Id = -1;
	int32 Machine = -1;
	int32 From = -1;
	int32 To = -1;
	bool bAny = false;
	// この矢印にまとめた遷移の Id（同じ元・同じ先・同じ種類の遷移は、1 本の矢印にまとめる。先頭は Id と同じ）
	TArray<int32> Ids;
	// 始点から終点（矢じり）までの折れ線。隣の点どうしは、縦か横に並ぶ
	TArray<ImVec2> Points;
	// ラベルを置く位置（経路の途中）
	ImVec2 Label = ImVec2( 0.0f, 0.0f );
	// 真ん中の線（Points[1] と Points[2] の間）を、他の矢印と重ならないようにずらしてよい範囲（縦の線なら x、横の線なら y）。MiddleMax <= MiddleMin なら動かさない
	float MiddleMin = 0.0f;
	float MiddleMax = 0.0f;
	// 箱に付いたまま、経路全体を平行移動してよい最大の幅（横に出る矢印なら上下、縦に出る矢印なら左右）。0 なら動かさない
	float ShiftLimit = 0.0f;
	// 中継ノードを置く位置（-1: 置かない、0: 真ん中の線の真ん中、1: Points[1]、2: Points[2]）
	int32 RelayAt = -1;
	// ラベルを、最後の線（Points[2] と Points[3] の間）の真ん中に置くか（false なら、真ん中の線の真ん中）
	bool bLabelOnLast = false;
};

// 中継ノード（矢印の分かれ目・合わさる所・長い矢印の途中）。通る矢印の Id を持つ
struct FStateGraphRelay
{
	ImVec2 Pos = ImVec2( 0.0f, 0.0f );
	TArray<int32> Ids;
};

struct FStateMachineGraphLayout
{
	// 定義のステートの番号ごとの、箱の左上と大きさ（入れ子のマシンの箱は、子を含む大きさ）
	TArray<ImVec2> Pos;
	TArray<ImVec2> Size;
	// 親のステートの番号 + 1 ごと（0 が最上位）
	TArray<FStateGraphMachine> Machines;
	TArray<FStateGraphRoute> Routes;
	TArray<FStateGraphRelay> Relays;
	// グラフ全体の大きさ（左上は 0, 0）
	ImVec2 Content = ImVec2( 0.0f, 0.0f );
};

// ビヘイビアツリーの配置
struct FBehaviorTreeGraphRelay
{
	ImVec2 Pos = ImVec2( 0.0f, 0.0f );
	// 分かれ目の親のノード
	int32 Parent = -1;
};

struct FBehaviorTreeGraphLayout
{
	// ノードの番号ごとの、箱の左上と大きさ
	TArray<ImVec2> Pos;
	TArray<ImVec2> Size;
	// ノードの番号ごとの、親からの線の折れ線（ルートは空）
	TArray<TArray<ImVec2>> Links;
	TArray<FBehaviorTreeGraphRelay> Relays;
	ImVec2 Content = ImVec2( 0.0f, 0.0f );
};

namespace NAIGraph
{
	// ステートマシンの配置と経路を求める。Transitions は CStateMachine::GetTransitions の結果
	FStateMachineGraphLayout BuildStateMachineLayout( const FStateMachineDefinition& Definition, const TArray<FStateTransitionInfo>& Transitions );

	// ビヘイビアツリーの配置と線を求める
	FBehaviorTreeGraphLayout BuildBehaviorTreeLayout( const FBehaviorTreeDefinition& Definition );

	// Parent の子のうち、State を含むもの（State が Parent の子孫でなければ -1）
	int32 TopChild( const FStateMachineDefinition& Definition, int32 Parent, int32 State );
}
