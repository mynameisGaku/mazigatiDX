// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIPanels.h"
#include "Library/Framework/UI/Core/UIScreenStack.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"

//=============================================================================
// CUIDialog
//   確認のダイアログ（タイトル・メッセージ・ボタンのいくつか）。画面（CUIScreenStack の 1 画面）として使う。
//   画面全体を暗幕（FUIDialogStyle::Dim。クリックを止める）で覆い、中央に窓を置く。
//
//   FUIDialogStyle Style;                                    // ブラシ・フォント・大きさ（FUIDefaultStyle の窓・ボタンを入れる）
//   TArray<FUIDialogButton> Buttons;
//   Buttons.Add( FUIDialogButton { "Yes", 1 } );
//   Buttons.Add( FUIDialogButton { "No", 2 } );
//   TSharedPtr<CUIDialog> Dialog = MakeShared<CUIDialog>( Style, "Quit?", "Unsaved changes will be lost.", Buttons, 1, 2 );
//   Dialog->OnResult.Add( []( CUIWidget&, int32 Id ) { if ( Id == 1 ) { ... } } );
//   NUIDialog::Show( Screens, Dialog );                      // モーダルな画面として積む。どれかを選ぶと、自動で閉じる
//
//   - 結果は OnResult（引数: ダイアログ・選んだボタンの番号）。ボタンのクリック・決定のほか、戻る（Escape・B ボタン）で CancelId が届く
//     （CancelId が -1 なら、戻るは、何も起こさず、ダイアログも閉じない。ダイアログは、戻るを使った扱い）。
//   - 最初にフォーカスがあるのは、DefaultId のボタン（Enter・A ボタンで、すぐに決められる）。矢印・Tab で、ほかのボタンへ動ける。
//   - Show は、画面を、モーダル・ゲームの入力を止める・戻るでは自動で外さない（結果で外す）設定で積む。Update の最中（ボタンのイベントの中）から呼んでもよい。
//=============================================================================
struct FUIDialogButton
{
	FString Text;
	int32 Id = 0;
};

struct FUIDialogStyle
{
	FUIBrush Dim;                  // 画面全体を覆う暗幕
	FUIBrush Window;               // 窓の背景（9 分割など）
	FUIButtonStyle Button;
	TSharedPtr<CFont> TitleFont;
	TSharedPtr<CFont> BodyFont;
	FColor32 TitleColor = FColor32( 255, 226, 150, 255 );
	FColor32 MessageColor = FColor32( 235, 238, 245, 255 );
	FVector2 WindowSize = FVector2( 460.0f, 240.0f );
	FVector2 ButtonSize = FVector2( 120.0f, 44.0f );
	FUIMargin Padding = FUIMargin( 28.0f, 24.0f );
	float32 ButtonSpacing = 14.0f;
};

class CUIDialog : public CUICanvasPanel
{
public:
	// DefaultId: 最初にフォーカスするボタンの番号（なければ、先頭のボタン）。CancelId: 戻るで届く番号（-1 は、戻るで何も起こさない）
	CUIDialog( const FUIDialogStyle& InStyle, const FString& InTitle, const FString& InMessage, const TArray<FUIDialogButton>& InButtons, int32 InDefaultId, int32 InCancelId = -1 );

	CUIButton* FindButton( int32 Id ) const;
	CUIButton* GetDefaultButton() const { return DefaultButton; }
	int32 GetCancelId() const { return CancelId; }
	CUIWidget* GetWindow() const { return Window.Get(); }
	CUILabel* GetTitleLabel() const { return TitleLabel.Get(); }
	CUILabel* GetMessageLabel() const { return MessageLabel.Get(); }

	// 戻る（Escape・B ボタン）を押したことにする（CancelId が 0 以上なら、OnResult が起こる）
	void Cancel();

	// 結果（引数: このダイアログ・選んだボタンの番号）
	TUIEvent<CUIWidget&, int32> OnResult;

private:
	struct FButtonEntry
	{
		int32 Id = 0;
		TSharedPtr<CUIButton> Button;
	};

	TSharedPtr<CUIWidget> Window;
	TSharedPtr<CUILabel> TitleLabel;
	TSharedPtr<CUILabel> MessageLabel;
	TArray<FButtonEntry> Buttons;
	CUIButton* DefaultButton = nullptr;
	int32 CancelId = -1;
};

namespace NUIDialog
{
	// ダイアログを、モーダルな画面として積み、既定のボタンにフォーカスを置く。どれかが選ばれたら（OnResult のあと）、画面を外す。戻り値は、画面の番号（積めなければ 0）。
	// Screens は、ダイアログが閉じるまで、生きていること
	int32 Show( CUIScreenStack& Screens, const TSharedPtr<CUIDialog>& Dialog );
}
