// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Core/Interfaces/Input/IClipboard.h"
#include "Library/Framework/Input/Text/TextEditBuffer.h"
#include "Library/Framework/UI/Brush/UIBrushFade.h"
#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUITextBox
//   1 行の入力欄。フォーカスがあるとき、ITextInput が届ける文字と編集の操作（かな漢字変換を含む）を受けて、文字列を編集する。
//   入力は CUIContext が渡す（結果の bWantsTextInput を、ホストが ITextInput::SetTextInputActive へ渡す）。
//   バッファは FTextEditBuffer（UTF-8・カーソルは文字の境目）。
//
//   TSharedPtr<CUITextBox> Name = MakeShared<CUITextBox>();
//   Name->SetFont( Font );
//   Name->SetPlaceholder( "Your name" );
//   Name->SetMaxLength( 16 );
//   Name->OnSubmitted.Add( []( CUIWidget&, const FString& Text ) { ... } );
//
//   - 操作: 文字・Backspace・Delete・左右・Home・End。Enter で確定（OnSubmitted を起こし、フォーカスを手放す）、Escape で取り消し
//     （フォーカスを得たときの文字列へ戻し、OnCancelled を起こして、フォーカスを手放す）。クリックで、その位置へカーソルを置く。
//   - 範囲選択: ドラッグで選ぶ。Shift + 左右・Home・End で広げる（ITextInput が SelectLeft などを届ける）、Ctrl + A ですべて選ぶ。選んだ範囲は、色で見せる。
//     コピー・切り取り・貼り付け（Ctrl + C / X / V）は、クリップボード（CUIContext に渡す FUIInputFrame::Clipboard）で行う。クリップボードが無ければ、選ぶだけ。
//     パスワード表示（SetPasswordMode）の欄は、コピー・切り取りをしない（貼り付けはできる）。貼り付ける文字列の改行は、空白にする。
//   - 文字列が欄より長いときは、カーソルが見える位置まで、横へずらして表示する（欄の矩形でクリップ）。
//   - かな漢字変換の途中の文字列は、カーソルの位置に差し込んで、下線つきで表示する。
//   - SetPasswordMode( true ) なら、文字を * で表示する（GetText は、本当の文字列）。
//   - カーソルの点滅は、描いた回数で数える（SetCaretBlinkFrames。0 なら、点滅しない）。文字やカーソルが動いた直後は、必ず見える。
//   - フォントが無いときは、何も描かない（編集はできる）。
//=============================================================================
struct FUITextBoxStyle
{
	FUIBrush Normal;
	FUIBrush Focused;
	FUIBrush Disabled;
	FColor32 TextColor = FColor32( 240, 244, 255, 255 );
	FColor32 PlaceholderColor = FColor32( 130, 140, 160, 255 );
	FColor32 DisabledTextColor = FColor32( 120, 124, 134, 255 );
	FColor32 CaretColor = FColor32( 255, 255, 255, 255 );
	FColor32 SelectionColor = FColor32( 70, 120, 210, 150 );   // 選んでいる範囲の背景
	FUIMargin TextPadding = FUIMargin( 10.0f, 4.0f );
	float32 TransitionDuration = 0.0f;   // 背景（通常・フォーカス中・使えない）の切り替えにかける秒（0 なら、すぐ）
};

class CUITextBox : public CUIWidget
{
public:
	CUITextBox();

	void SetFont( const TSharedPtr<CFont>& InFont );
	void SetStyle( const FUITextBoxStyle& InStyle ) { Style = InStyle; }
	const FUITextBoxStyle& GetStyle() const { return Style; }

	// 文字列を置き換える（上限を超える分は切る）。変わったら OnTextChanged（bNotify が false なら起こさない）。カーソルは末尾へ
	void SetText( const FString& InText, bool bNotify = true );
	const FString& GetText() const { return Buffer.GetText(); }
	// 何も入っていない・フォーカスも無いときに、薄く出す文字
	void SetPlaceholder( const FString& InPlaceholder ) { Placeholder = InPlaceholder; }
	const FString& GetPlaceholder() const { return Placeholder; }
	// 文字数の上限（0 は上限なし）
	void SetMaxLength( int32 InMaxLength ) { Buffer.SetMaxCharCount( InMaxLength ); }
	void SetPasswordMode( bool bInPassword ) { bPasswordMode = bInPassword; }
	void SetCaretBlinkFrames( int32 InFrames ) { CaretBlinkFrames = NMath::Max( 0, InFrames ); }

	// カーソルの位置（先頭からのバイト数。文字の境目）
	int32 GetCursorByteOffset() const { return Buffer.GetCursorByteOffset(); }
	void SetCursorByteOffset( int32 InOffset );
	// いま、カーソルを描く回か（点滅の位相。フォーカスが無ければ false）
	bool IsCaretVisible() const;

	// 範囲選択（先頭からのバイト数。文字の境目）
	bool HasSelection() const { return Buffer.HasSelection(); }
	FString GetSelectedText() const { return Buffer.GetSelectedText(); }
	void SetSelection( int32 InAnchorByteOffset, int32 InCursorByteOffset );
	void SelectAll();
	int32 GetSelectionStart() const { return Buffer.GetSelectionStart(); }
	int32 GetSelectionEnd() const { return Buffer.GetSelectionEnd(); }
	// 表示している文字列（* で隠したもの・かな漢字変換の途中の文字を差し込んだもの）と、その中のカーソルの位置（バイト）
	FString GetDisplayText( int32& OutCursorByteOffset ) const;

	// 文字列が変わったとき（引数: この欄・新しい文字列）
	TUIEvent<CUIWidget&, const FString&> OnTextChanged;
	// 確定（Enter）されたとき
	TUIEvent<CUIWidget&, const FString&> OnSubmitted;
	// 取り消し（Escape）されたとき
	TUIEvent<CUIWidget&> OnCancelled;

	bool WantsTextInput() const override { return IsEnabled(); }

protected:
	FVector2 ComputeContentSize() const override;
	void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity ) override;
	void OnAdvanceTime( float32 DeltaTime ) override;
	void OnPointerPressed( const FVector2& Position ) override;
	void OnPointerDragged( const FVector2& Position ) override;
	void OnPointerReleased( const FVector2& Position ) override;
	void SetTextInputClipboard( IClipboard* InClipboard ) override { Clipboard = InClipboard; }
	bool OnTextInput( const TArray<FTextInputEvent>& Events, const FImeComposition* Ime ) override;

private:
	// 実際の文字列の先頭 ByteOffset バイトを、表示するときの幅（画素）
	float32 PrefixWidth( int32 ByteOffset ) const;
	FUIRect GetTextArea() const { return GetRect().Deflate( Style.TextPadding ); }
	// 画面の x 座標にいちばん近い、文字の境目（バイト）
	int32 ByteOffsetAt( float32 PointerX ) const;
	// 範囲をクリップボードへ（コピー）。入れたら true
	bool CopySelection();

	TSharedPtr<CFont> Font;
	// 背景の状態の番号（0 = 通常、1 = フォーカス中、2 = 使えない）と、そのブラシ
	int32 GetBackgroundStateIndex() const;
	const FUIBrush& GetBackgroundBrush( int32 StateIndex ) const;

	FUITextBoxStyle Style;
	FUIStateFade BackgroundFade;
	FTextEditBuffer Buffer;
	FString Placeholder;
	FString TextAtFocus;
	FString CompositionText;
	int32 CompositionCursor = 0;
	bool bPasswordMode = false;
	int32 CaretBlinkFrames = 30;
	int32 PaintCounter = 0;
	float32 ScrollX = 0.0f;
	IClipboard* Clipboard = nullptr;   // このフレームの間だけ有効（CUIContext が、文字入力を渡す直前に決める）
	bool bDragSelecting = false;
	int32 DragAnchor = 0;
};
