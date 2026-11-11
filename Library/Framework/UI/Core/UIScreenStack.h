// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Math/Easing.h"
#include "Library/Framework/UI/Core/UIContext.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"

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
//   - 積む・外す（Push・Pop・Remove・Clear）を、Update の最中（ボタンのイベントの中など）に呼んでも安全。その場合は、Update の終わりに、呼んだ順で反映する
//     （Push の戻り値の番号は、すぐに使える。Num・GetTopRoot などは、反映されるまで、前の状態を返す）。
//   - 画面ごとに、自分のコンテキスト（ホバー・フォーカス）を持つ。下の画面の状態は、上に別の画面が載っている間も残る
//     （入力が届かない間は、ホバーなどが外れる）。
//   - モーダルな画面（既定）は、下の画面へ、ポインターもナビゲーションも渡さない。モーダルでない画面は、自分の部品の上にポインターが
//     無いときだけ、ポインターを下の画面へ渡す。ナビゲーション（矢印・Tab・決定・戻る）は、いちばん上の画面にだけ渡す。
//   - 戻る（Escape・B ボタン）が押され、いちばん上の画面が bPopOnCancel なら、その画面を外す。
//   - 出入りの演出（FUIScreenOptions::Transition）: 消える演出の間の画面は、まだ描かれるが、入力は受けず、ゲームの入力も止めず、「いちばん上」にも数えない。
//     演出が終わったときに、実際に外れて、OnPopped が起きる（Num は、それまで、その画面も数える。NumOpen は、数えない）。Clear は、演出なしで、すぐ外す。
//   - ツールチップ: 部品の SetTooltip（文字）か SetTooltipWidget（好きな部品。木に入れない）の指定を、ポインターがしばらく止まったときに、全部の画面の上へ描く（Paint の最後）。
//     SetTooltipStyle で、文字のツールチップの見た目（背景のブラシ・フォント・余白・待ち時間など）を決める。フォントが無ければ、文字のツールチップは出さない。
//     画面の端からはみ出すときは、ポインターの反対側（左・上）へ寄せて、画面の中に収める。時間は、FUIInputFrame::DeltaTime で数える。
//=============================================================================
// 画面の出入りの演出
enum class EUIScreenTransitionKind : uint8
{
	None,        // すぐ出る・すぐ消える
	Fade,        // 不透明度
	SlideUp,     // 下から上がってくる（消えるときは、下へ）
	SlideDown,   // 上から降りてくる
	SlideLeft,   // 右から入ってくる
	SlideRight,  // 左から入ってくる
	Scale        // 少し小さいところから、広がる
};

// 出るとき（Push）は 0 → 1、消えるとき（Pop・Remove・戻る）は 1 → 0 へ、Duration 秒で動く。どの演出も、不透明度も一緒に変わる。
// 演出の間、スタックが、ルートの不透明度と描くときの変換（SetOpacity・SetRenderTransform）を決める。
struct FUIScreenTransition
{
	EUIScreenTransitionKind Kind = EUIScreenTransitionKind::None;
	float32 Duration = 0.2f;                       // 秒（0 以下なら、演出なし）
	EEasing Easing = EEasing::CubicOut;
	float32 Distance = 48.0f;                      // Slide: ずらす画素
	float32 StartScale = 0.9f;                     // Scale: 始まりの倍率
};

struct FUIScreenOptions
{
	bool bModal = true;             // 下の画面へ、入力を渡さない
	bool bBlocksGameInput = true;   // この画面がある間、ゲーム自身の入力を止める
	bool bPopOnCancel = true;       // 戻る（Escape・B ボタン）で、この画面を外す（いちばん上のときだけ）
	CUIWidget* InitialFocus = nullptr;   // 積まれたとき、最初にフォーカスを持たせる部品（この画面の木の中。null なら、フォーカスなし）
	FUIScreenTransition Transition;      // 出入りの演出（既定は、なし）
};

// 文字のツールチップの見た目
struct FUITooltipStyle
{
	FUIBrush Background;                                     // 背景（9 分割など）
	TSharedPtr<CFont> Font;                                  // 文字のフォント（無ければ、文字のツールチップは出さない）
	FColor32 TextColor = FColor32( 240, 244, 255, 255 );
	FUIMargin Padding = FUIMargin( 10.0f, 6.0f );            // 背景の内側の余白
	float32 MaxWidth = 320.0f;                               // 文字の折り返す幅（0 なら、折り返さない）
	float32 Delay = 0.5f;                                    // 出すまでの待ち時間（秒）
	FVector2 Offset = FVector2( 14.0f, 22.0f );              // ポインターから、ツールチップの左上までのずれ
};

struct FUIScreenInputResult
{
	bool bPointerOverUI = false;        // ポインターが、どれかの画面の、当たる部品の上にある
	bool bPointerCaptured = false;      // 部品を押している最中
	bool bNavigationConsumed = false;   // ナビゲーション・決定・戻るを、UI が使った
	bool bWheelConsumed = false;        // ホイールを、UI が使った
	bool bWantsTextInput = false;       // 文字入力を受ける部品（入力欄）にフォーカスがある（ホストは、ITextInput::SetTextInputActive にこの値を渡す）
	bool bGameInputBlocked = false;     // ゲーム自身の入力を止めるべきか（止める画面がある・ポインターを UI が使っている・ナビゲーションを UI が使った）

	bool ConsumesPointer() const { return bPointerOverUI || bPointerCaptured; }
};

class CUIScreenStack
{
public:
	// 画面の矩形（すべての画面を、この大きさで配置する）
	void SetArea( const FUIRect& InArea ) { Area = InArea; }
	const FUIRect& GetArea() const { return Area; }

	// 画面を、いちばん上へ積む。戻り値は、画面の番号（1 以上。Remove に使う）。Root が null なら 0。Update の最中なら、Update の終わりに積まれる
	int32 Push( const TSharedPtr<CUIWidget>& Root, const FUIScreenOptions& Options = FUIScreenOptions() );
	// いちばん上の画面を外す。画面が無ければ false。Update の最中なら、Update の終わりに外れる
	bool Pop();
	// 番号の画面を外す（上でなくてもよい）。なければ false。Update の最中なら、Update の終わりに外れる
	bool Remove( int32 Id );
	void Clear();

	int32 Num() const { return Screens.Num(); }
	bool IsEmpty() const { return Screens.IsEmpty(); }
	// 消える演出の途中の画面を除いた数
	int32 NumOpen() const;
	// 番号の画面が、消える演出の途中か
	bool IsClosing( int32 Id ) const;
	// いちばん上の画面のルート・コンテキスト（無ければ null）
	CUIWidget* GetTopRoot() const;
	CUIContext* GetTopContext() const;
	// 番号の画面のコンテキスト（無ければ null。フォーカスを決めるときなどに）
	CUIContext* FindContext( int32 Id ) const;

	// 入力を、上の画面から順に渡す（画面ごとに、レイアウトも更新する）
	FUIScreenInputResult Update( const FUIInputFrame& Frame );
	// 下から順に、描く命令を記録する
	void Paint( FDrawContext& CommandList );

	// 文字のツールチップの見た目（既定は、フォントなし = 文字のツールチップは出ない）
	void SetTooltipStyle( const FUITooltipStyle& InStyle );
	const FUITooltipStyle& GetTooltipStyle() const { return TooltipStyle; }
	// いま出ているツールチップの部品（出ていなければ null。確認用）
	const TSharedPtr<CUIWidget>& GetShownTooltip() const { return ShownTooltip; }
	// 出ているツールチップの矩形（Paint のあとで有効。出ていなければ、大きさ 0）
	const FUIRect& GetShownTooltipRect() const { return ShownTooltipRect; }

	// 画面が外れたとき（Pop・Remove・戻る・Clear）。引数は、画面の番号
	TUIEvent<int32> OnPopped;

private:
	struct FScreen
	{
		int32 Id = 0;
		TSharedPtr<CUIWidget> Root;
		FUIScreenOptions Options;
		TSharedPtr<CUIContext> Context;
		float32 TransitionProgress = 1.0f;   // 0（見えない）〜 1（出きった）
		bool bClosing = false;               // 消える演出の途中
	};

	// Update の最中に呼ばれた、積む・外す。Update の終わりに、呼ばれた順で反映する
	struct FPendingChange
	{
		bool bPush = false;
		FScreen Screen;
		int32 RemoveId = 0;
	};

	bool RemoveNow( int32 Id );
	// 演出なしで、すぐ外す
	bool RemoveImmediately( int32 Id );
	// 演出を進めて、ルートの見た目を決める。消えきった画面を外す
	void AdvanceTransitions( float32 DeltaTime );
	static void ApplyTransition( FScreen& Screen );
	// いちばん上の、消える途中でない画面の位置（無ければ -1）
	int32 FindTopOpenIndex() const;
	void UpdateTooltip();
	void AddScreen( const FScreen& Screen );
	void ApplyPending();
	bool IsKnown( int32 Id ) const;

	TArray<FScreen> Screens;
	TArray<FPendingChange> Pending;
	bool bUpdating = false;
	FUIRect Area = FUIRect( 0.0f, 0.0f, 1280.0f, 720.0f );
	int32 NextId = 1;

	// ツールチップ
	FUITooltipStyle TooltipStyle;
	TSharedPtr<CUIWidget> TextTooltip;          // 文字のツールチップ用の部品（背景 + ラベル。使い回す）
	TSharedPtr<CUILabel> TextTooltipLabel;
	TSharedPtr<CUIWidget> ShownTooltip;         // いま出すもの（Update で決め、Paint で描く）
	FVector2 ShownTooltipAnchor = FVector2( 0.0f, 0.0f );
	FUIRect ShownTooltipRect;
};
