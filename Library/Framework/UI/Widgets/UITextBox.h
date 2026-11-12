// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Core/Interfaces/Input/IClipboard.h"
#include "Library/Framework/Input/Text/TextEditBuffer.h"
#include "Library/Framework/UI/Brush/UIBrushFade.h"
#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUITextBox
//   入力欄（1 行。SetMultiline で、複数行）。フォーカスがあるとき、ITextInput が届ける文字と編集の操作（かな漢字変換を含む）を受けて、文字列を編集する。
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
//     ダブルクリックで、単語を選ぶ（そのままドラッグすると、単語ごとに広がる）。3 回目のクリックで、全体（複数行の欄では、その行）を選ぶ。
//     クリックの間隔は、SetDoubleClickSeconds（既定 0.4 秒）。時間は、CUIWidget::AdvanceTime（CUIContext::Update が呼ぶ）で進む。時間が進まないホストでは、ダブルクリックは起こらない。
//   - 文字列が欄より長いときは、カーソルが見える位置まで、横へずらして表示する（欄の矩形でクリップ）。
//   - 複数行（SetMultiline( true )）: Enter で改行を入れる（確定の OnSubmitted は、起こらない。中身は OnTextChanged で受ける）。上下で、行を移る（Shift つきなら、範囲を広げる）。
//     Home / End は、行の端。貼り付けは、改行を残す。行は、欄の上から並べ、カーソルが見える位置まで、縦にもずらす。ホイールでも、縦に動く。
//     SetWordWrap( true ) で、欄の幅で折り返す（空白の後ろか、日本語などの文字の間で折る。禁則は、しない。横へは、ずれない）。
//     上下・クリック・3 回目のクリックは、折り返した見た目の行で働く。Home / End は、改行までの行の端。折り返さなければ、長い行は、横へずれる。フォーカス中は、上下の矢印を欄が使うので、フォーカスを移すには、Tab か、Escape（取り消し）を使う。
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
	// 複数行の欄にするか（上の説明）。1 行へ戻しても、入っている改行は、消さない
	void SetMultiline( bool bInMultiline );
	bool IsMultiline() const { return Buffer.IsMultiline(); }
	// 複数行の欄で、欄の幅で折り返すか（既定は、折り返さない。1 行の欄では、何もしない）
	void SetWordWrap( bool bInWordWrap );
	bool IsWordWrap() const { return bWordWrap; }
	// 見た目の行の数（折り返した分も数える。1 行の欄では 1。フォントと、置いたあとの矩形で決まる）
	int32 GetNumVisualLines() const;
	// ダブルクリックと見なす、クリックの間隔（秒。0 以下なら、ダブルクリックをしない）
	void SetDoubleClickSeconds( float32 InSeconds ) { DoubleClickSeconds = InSeconds; }
	float32 GetDoubleClickSeconds() const { return DoubleClickSeconds; }
	// 縦にずらしている量（画素。複数行の欄）
	float32 GetScrollY() const { return ScrollY; }
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
	bool OnNavigate( EUINavDirection Direction ) override;
	bool OnWheel( const FVector2& Position, float32 Delta ) override;

private:
	// 1 行の範囲（バイト。改行は含まない）
	struct FLine
	{
		int32 Start = 0;
		int32 End = 0;
	};
	// 文字列を、見た目の行に分ける（1 行の欄では、全体で 1 行。複数行の欄では、改行と、折り返しで分ける）
	void BuildLines( TArray<FLine>& OutLines ) const;
	// 実際の文字列の [Start, End) を、表示するときの文字列（パスワードなら *）と、その幅（画素）
	FString GetShownRange( int32 Start, int32 End ) const;
	float32 MeasureRange( int32 Start, int32 End ) const;
	float32 GetLineHeight() const;
	// LineIndex 行目の文字の上の y（1 行の欄では、欄の中央に置いた行）
	float32 GetLineTop( int32 LineIndex ) const;
	FUIRect GetTextArea() const { return GetRect().Deflate( Style.TextPadding ); }
	// 行の中で、行の先頭からの幅 LocalX にいちばん近い、文字の境目（バイト）
	int32 ByteOffsetInLine( const FLine& Line, float32 LocalX ) const;
	// 画面の位置にいちばん近い、文字の境目（バイト）
	int32 ByteOffsetAt( const FVector2& Position ) const;
	// カーソルを、上（-1）・下（+1）の行へ動かす（複数行の欄）。bSelect なら、範囲を広げる
	void MoveCaretVertically( int32 Direction, bool bSelect );
	// 文字やカーソルが動いた: カーソルを見せ、見える位置までずらす
	void MarkCaretMoved();
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
	float32 ScrollY = 0.0f;
	bool bScrollToCaret = false;       // 次に描くとき、カーソルが見える位置まで、縦にずらす
	bool bWordWrap = false;
	float32 DoubleClickSeconds = 0.4f;
	float32 TimeSincePress = 0.0f;     // 最後に押してからの秒
	FVector2 LastPressPosition = FVector2( 0.0f, 0.0f );
	int32 ClickCount = 0;              // 続けて押した回数（1 = クリック、2 = ダブルクリック、3 = 3 回目）
	bool bDragByWord = false;          // ダブルクリックからのドラッグ（単語ごとに広げる）
	int32 DragWordStart = 0;
	int32 DragWordEnd = 0;
	IClipboard* Clipboard = nullptr;   // このフレームの間だけ有効（CUIContext が、文字入力を渡す直前に決める）
	bool bDragSelecting = false;
	int32 DragAnchor = 0;
};
