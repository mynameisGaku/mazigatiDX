// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// ノードグラフの描画部品
//   箱（ノード）と矢印（リンク）を、パン（ドラッグ）とズーム（ホイール）のできるキャンバスに描く。ImDrawList だけで作ってあり、
//   外部のノードエディタ（ImNodes / imgui-node-editor）は使わない（VendorModules に含まれておらず、ビヘイビアツリーの自動配置とステートマシンの
//   自由配置の両方を同じ土台で扱いたいため）。AI デバッガのグラフ表示が使い、将来のエディタも同じ部品を使う。
//
//   座標は「ワールド座標」（グラフ上の位置。ズームやパンの影響を受けない）で渡す。画面への変換はキャンバスが行う。
//
//   FNodeGraphView View;                                          // パンとズームを覚えておく（ウィンドウごとに 1 つ）
//   FNodeGraphCanvas Canvas;
//   if ( Canvas.Begin( "##graph", View, ImVec2( 0.0f, 320.0f ), ContentSize ) )
//   {
//       Canvas.DrawBox( ImVec2( 0, 0 ), ImVec2( 150, 48 ), Fill, Border, 2.0f );
//       Canvas.DrawText( ImVec2( 8, 8 ), TextColor, "Idle" );
//       const ImVec2 Route[4] = { From, ImVec2( MidX, From.y ), ImVec2( MidX, To.y ), To };
//       Canvas.DrawElbowArrow( Route, 4, Color, 2.0f );     // 直角に折れる線。角は丸め、終点に矢じり
//       Canvas.DrawRelay( ImVec2( MidX, From.y ), Color );  // 中継ノード（分かれる・合わさる所の丸い点）
//       Canvas.End();
//   }
//=============================================================================
struct FNodeGraphView
{
	// パン（ワールド座標での平行移動）とズーム
	ImVec2 Pan = ImVec2( 0.0f, 0.0f );
	float Zoom = 1.0f;
	// 全体が収まるように合わせたか。false にすると、次の Begin で合わせ直す
	bool bFitted = false;
	// 合わせたときの、グラフ全体の大きさ（変わったら、合わせ直す）
	ImVec2 FittedContent = ImVec2( 0.0f, 0.0f );
	// 合わせたときの、キャンバスの大きさ（ウィンドウの大きさが変わったら、合わせ直す）
	ImVec2 FittedCanvas = ImVec2( 0.0f, 0.0f );
	// true の間は、ドラッグでパンしない（ノードを動かすなど、キャンバスの上の操作が優先されるとき。エディタが、毎フレーム、次のフレームの分を設定する）
	bool bPanBlocked = false;
};

class FNodeGraphCanvas
{
public:
	// キャンバスの範囲を確保して、パン（左ボタンか中ボタンのドラッグ）・ズーム（ホイール）を処理する。
	// Size の 0 以下の成分は、残りの幅・高さいっぱい。ContentSize はグラフ全体の大きさ（ワールド座標）で、
	// 最初と、これが変わったときに、全体が収まるようにパンとズームを合わせる。小さすぎて描けなければ false（End は呼ばない）
	bool Begin( const char* Id, FNodeGraphView& View, const ImVec2& Size, const ImVec2& ContentSize );
	void End();

	ImVec2 ToScreen( const ImVec2& World ) const;
	// 画面の座標から、ワールド座標へ
	ImVec2 ToWorld( const ImVec2& Screen ) const;
	float GetZoom() const { return View != nullptr ? View->Zoom : 1.0f; }
	// マウスが、キャンバスの上にあるか
	bool IsHovered() const { return bHovered; }

	// 角丸の箱。戻り値: マウスが乗っているか
	bool DrawBox( const ImVec2& WorldMin, const ImVec2& WorldSize, ImU32 Fill, ImU32 Border, float BorderThickness, float Rounding = 6.0f );
	// 箱の上辺に沿った色の帯（結果の色など）
	void DrawBand( const ImVec2& WorldMin, float WorldWidth, float WorldHeight, ImU32 Color, float Rounding = 6.0f );
	// 文字。FontSize は、ズーム 1 のときの大きさ（0 以下で、ImGui の既定）。ズームが小さいと、読めないので描かない。Clip は、はみ出しを切る範囲（ワールド座標。幅 0 なら切らない）
	void DrawText( const ImVec2& WorldPos, ImU32 Color, const char* Text, float FontSize = 0.0f, const ImVec2& ClipMin = ImVec2( 0.0f, 0.0f ), const ImVec2& ClipSize = ImVec2( 0.0f, 0.0f ) );
	// 文字の幅（ワールド座標）
	static float MeasureText( const char* Text, float FontSize = 0.0f );
	// 矢じりつきの、直角に折れる線（Points は、始点から終点までの折れ線の点。ワールド座標。隣の点どうしは、縦か横に並べる）。
	// 角は、CornerRadius（ワールド座標）で丸める。戻り値: マウスが線の近くにあるか
	bool DrawElbowArrow( const ImVec2* Points, int32 NumPoints, ImU32 Color, float Thickness, float CornerRadius = 9.0f );
	// 中継ノード（矢印が分かれる・合わさる所や、長い矢印の途中に置く、小さな丸）。戻り値: マウスが乗っているか
	bool DrawRelay( const ImVec2& WorldPos, ImU32 Color, float Radius = 5.0f );
	// 塗りつぶした小さな点（UML の初期状態の印。入口の矢印の始点に使う）
	void DrawDot( const ImVec2& WorldPos, ImU32 Color, float Radius = 4.0f );
	// 枠だけ（選択・ドロップ先の強調など）。線（ドロップ位置の印など）
	void DrawOutline( const ImVec2& WorldMin, const ImVec2& WorldSize, ImU32 Color, float Thickness, float Rounding = 6.0f );
	void DrawLine( const ImVec2& WorldA, const ImVec2& WorldB, ImU32 Color, float Thickness );

private:
	FNodeGraphView* View = nullptr;
	ImDrawList* DrawList = nullptr;
	// キャンバスの左上（画面座標）と大きさ
	ImVec2 Origin = ImVec2( 0.0f, 0.0f );
	ImVec2 CanvasSize = ImVec2( 0.0f, 0.0f );
	bool bHovered = false;
	bool bBegun = false;
};

namespace NNodeGraph
{
	// 色 A から B へ、T（0..1）で混ぜる
	ImU32 Lerp( ImU32 A, ImU32 B, float T );
}
