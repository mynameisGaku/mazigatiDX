// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// FUIInputFrame
//   1 フレーム分の入力の状態（入力の窓口から、ホストが埋めて渡す。UI は、OS・DxLib・入力の実装を知らない）。
//   ボタンは「いま押されているか」の状態（押した瞬間・離した瞬間は、CUIContext が前のフレームとの差で見つける）。
//   ナビゲーションの項目は「このフレームで押された」の印（キーのリピートは、ホストが決める）。
//=============================================================================
struct FUIInputFrame
{
	// ポインター（マウス・タッチ）。bPointerValid が false なら、ポインターは画面にない（ホバーは外れる）
	FVector2 PointerPosition = FVector2( 0.0f, 0.0f );
	bool bPointerValid = true;
	bool bPointerDown = false;      // 主ボタン（左・タッチ）
	bool bPointerSecondaryDown = false; // 副ボタン（右）。いまはイベントを起こさず、「UI の上で押された」の判定にだけ使う

	// ナビゲーション（このフレームで押された）
	bool bNavigateUp = false;
	bool bNavigateDown = false;
	bool bNavigateLeft = false;
	bool bNavigateRight = false;
	bool bConfirm = false;          // 決定（Enter・A ボタン）
	bool bCancel = false;           // 戻る（Esc・B ボタン）
	bool bFocusNext = false;        // フォーカスを次へ（Tab）
	bool bFocusPrevious = false;    // フォーカスを前へ（Shift + Tab）
};

// Update の結果。ゲームは、これを見て、入力を UI が食べたかを知る（UI の上でクリックしたとき、攻撃が出ないように）
struct FUIInputResult
{
	bool bPointerOverUI = false;        // ポインターが、当たり判定のある部品の上にある
	bool bPointerCaptured = false;      // 部品を押している最中（ドラッグ中）
	bool bNavigationConsumed = false;   // ナビゲーション・決定・戻るを、UI が使った（フォーカスを動かした・決定した・戻るの受け手がいた）
	bool bCancelRequested = false;      // 戻るが押された（受け手の有無に関わらず）

	// ポインターの入力を、UI が使っているか（ゲームの側が、クリックなどを無視するかの判断に）
	bool ConsumesPointer() const { return bPointerOverUI || bPointerCaptured; }
};

//=============================================================================
// CUIContext
//   部品の木（ルート）に、入力を配る。ホバー・押下（キャプチャ）・クリック・フォーカス（矢印・Tab・決定）・戻るを扱い、
//   部品のイベント（OnClicked など）を起こす。毎フレーム、UpdateLayout のあとに Update を呼ぶ。
//
//   CUIContext Context;
//   Context.SetRoot( Root );
//   Root->UpdateLayout( Screen );
//   const FUIInputResult Result = Context.Update( Frame );
//   if ( !Result.ConsumesPointer() ) { /* ゲームの入力へ */ }
//
//   - 部品は、ルートの木に入っている間だけ有効（木から外された部品が、ホバー・押下・フォーカスを持っていたら、イベントなしで外す）。
//   - ポインターの対象は、当たった部品から親へたどって、最初の Interactive な部品。使えない（Disabled）部品は、イベントを受けず、入力を止めるだけ。
//   - フォーカスを動かす: 矢印は、部品の位置から自動で選ぶ（向きの半平面にある、いちばん近い Focusable。部品ごとの名前の指定が先）。
//     Tab / Shift + Tab は、木の順（先に追加した順）で巡る。フォーカスが無いときの最初の操作は、先頭（Tab 前は末尾）へ。
//=============================================================================
class CUIContext
{
public:
	void SetRoot( const TSharedPtr<CUIWidget>& InRoot );
	CUIWidget* GetRoot() const { return Root.Get(); }

	// 入力を配る。戻り値は、UI が入力を使ったか（上の説明）
	FUIInputResult Update( const FUIInputFrame& Frame );

	CUIWidget* GetHovered() const { return Hovered; }
	CUIWidget* GetPressed() const { return Pressed; }
	CUIWidget* GetFocused() const { return Focused; }

	// フォーカスを動かす。木にない・Focusable でない・使えない・見えない部品なら、false（フォーカスは変わらない）。nullptr で外す
	bool SetFocus( CUIWidget* Widget );

	// 戻る（Cancel）が押されたときのイベント（画面を 1 つ戻す、などに使う）。受け手がいれば、戻るを使ったことになる
	TUIEvent<> OnCancel;

private:
	bool IsInTree( const CUIWidget* Widget ) const;
	bool IsLive( const CUIWidget* Widget ) const;
	void CollectFocusable( CUIWidget& Widget, TArray<CUIWidget*>& OutList ) const;
	CUIWidget* FindNavigationTarget( CUIWidget& From, EUINavDirection Direction ) const;
	CUIWidget* FindNearestInDirection( CUIWidget& From, EUINavDirection Direction ) const;
	void Press( CUIWidget& Target );
	void Release( CUIWidget* Over );
	void Activate( CUIWidget& Target );

	TSharedPtr<CUIWidget> Root;
	CUIWidget* Hovered = nullptr;
	CUIWidget* Pressed = nullptr;
	CUIWidget* Focused = nullptr;
	bool bPreviousPointerDown = false;
};
