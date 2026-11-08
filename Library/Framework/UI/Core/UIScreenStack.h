// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIContext.h"

//=============================================================================
// CUIScreenStack
//   画面（部品の木のルート）を積み重ねて、描く順と、入力の行き先を決める（HUD → ポーズのメニュー → 確認のダイアログ、など）。
//   入力は、上の画面から先に渡す。上の画面が使った入力（クリックなど）は、下の画面へは届かない。
//   ゲームは、結果の bGameInputBlocked を見て、メニューを開いている間などに、ゲーム自身の入力を止められる。
//
//   CUIScreenStack Screens;
//   Screens.SetArea( FUIRect( 0, 0, 1280, 720 ) );
//   Screens.Push( Hud, FUIScreenOptions { false, false, false } );      // HUD: モーダルでなく、ゲームの入力も止めない
//   Screens.Push( PauseMenu );                                          // メニュー: モーダル・ゲームの入力を止める・Escape で閉じる
//
//   const FUIScreenInputResult Result = Screens.Update( Frame );        // 毎フレーム（レイアウトも、ここで更新する）
//   if ( !Result.bGameInputBlocked ) { /* ゲームの入力 */ }
//   Screens.Paint( CommandList );                                       // 下から順に描く
//
//   - 画面ごとに、自分のコンテキスト（ホバー・フォーカス）を持つ。下の画面の状態は、上に別の画面が載っている間も残る
//     （入力が届かない間は、ホバーなどが外れる）。
//   - モーダルな画面（既定）は、下の画面へ、ポインターもナビゲーションも渡さない。モーダルでない画面は、自分の部品の上にポインターが
//     無いときだけ、ポインターを下の画面へ渡す。ナビゲーション（矢印・Tab・決定・戻る）は、いちばん上の画面にだけ渡す。
//   - 戻る（Escape・B ボタン）が押され、いちばん上の画面が bPopOnCancel なら、その画面を外す。
//=============================================================================
struct FUIScreenOptions
{
	bool bModal = true;             // 下の画面へ、入力を渡さない
	bool bBlocksGameInput = true;   // この画面がある間、ゲーム自身の入力を止める
	bool bPopOnCancel = true;       // 戻る（Escape・B ボタン）で、この画面を外す（いちばん上のときだけ）
};

struct FUIScreenInputResult
{
	bool bPointerOverUI = false;        // ポインターが、どれかの画面の、当たる部品の上にある
	bool bPointerCaptured = false;      // 部品を押している最中
	bool bNavigationConsumed = false;   // ナビゲーション・決定・戻るを、UI が使った
	bool bWheelConsumed = false;        // ホイールを、UI が使った
	bool bGameInputBlocked = false;     // ゲーム自身の入力を止めるべきか（止める画面がある・ポインターを UI が使っている・ナビゲーションを UI が使った）

	bool ConsumesPointer() const { return bPointerOverUI || bPointerCaptured; }
};

class CUIScreenStack
{
public:
	// 画面の矩形（すべての画面を、この大きさで配置する）
	void SetArea( const FUIRect& InArea ) { Area = InArea; }
	const FUIRect& GetArea() const { return Area; }

	// 画面を、いちばん上へ積む。戻り値は、画面の番号（1 以上。Remove に使う）。Root が null なら 0
	int32 Push( const TSharedPtr<CUIWidget>& Root, const FUIScreenOptions& Options = FUIScreenOptions() );
	// いちばん上の画面を外す。画面が無ければ false
	bool Pop();
	// 番号の画面を外す（上でなくてもよい）。なければ false
	bool Remove( int32 Id );
	void Clear();

	int32 Num() const { return Screens.Num(); }
	bool IsEmpty() const { return Screens.IsEmpty(); }
	// いちばん上の画面のルート・コンテキスト（無ければ null）
	CUIWidget* GetTopRoot() const;
	CUIContext* GetTopContext() const;
	// 番号の画面のコンテキスト（無ければ null。フォーカスを決めるときなどに）
	CUIContext* FindContext( int32 Id ) const;

	// 入力を、上の画面から順に渡す（画面ごとに、レイアウトも更新する）
	FUIScreenInputResult Update( const FUIInputFrame& Frame );
	// 下から順に、描く命令を記録する
	void Paint( FDrawContext& CommandList );

	// 画面が外れたとき（Pop・Remove・戻る・Clear）。引数は、画面の番号
	TUIEvent<int32> OnPopped;

private:
	struct FScreen
	{
		int32 Id = 0;
		TSharedPtr<CUIWidget> Root;
		FUIScreenOptions Options;
		TSharedPtr<CUIContext> Context;
	};

	TArray<FScreen> Screens;
	FUIRect Area = FUIRect( 0.0f, 0.0f, 1280.0f, 720.0f );
	int32 NextId = 1;
};
