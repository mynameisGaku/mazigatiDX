// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Input/Text/TextInputTypes.h"

//=============================================================================
// FTextEditBuffer
//   文字入力の欄の中身（UTF-8 の文字列とカーソル）。ITextInput のイベントを渡すと、文字が入り、消え、カーソルが動く。
//   バックエンドに依らない（イベントだけを見る）。描画や IME の変換中の表示は、呼ぶ側が行う。
//
//   FTextEditBuffer Field;
//   Field.SetMaxCharCount( 16 );
//   Field.ApplyAll( TextInput->GetTextInputEvents() );
//   if ( Field.ConsumeSubmitted() ) { ... Field.GetText() ... }
//
//   - カーソルは文字の境目にだけ置く（文字の途中のバイトには止まらない）。位置は先頭からのバイト数
//   - Enter: 1 行の欄では確定の合図（ConsumeSubmitted で 1 度だけ true）、複数行の欄では改行を入れる
//   - Escape: 取り消しの合図（ConsumeCancelled で 1 度だけ true）。文字は変えない
//   - 上下・Tab・ページ・コピーと貼り付け（クリップボードは扱わない）は、何もしない
//   - 文字数の上限は MaxCharCount（0 は上限なし）。超える分の文字は入れない
//=============================================================================
class FTextEditBuffer
{
public:
	// 文字数の上限（0 は上限なし）。いまの文字列が超えていれば、後ろを切る
	void SetMaxCharCount( int32 InMaxCharCount );
	int32 GetMaxCharCount() const
	{
		return MaxCharCount;
	}

	// 複数行の欄にするか（Enter で改行を入れる。Home / End は行の端）
	void SetMultiline( bool bInMultiline )
	{
		bMultiline = bInMultiline;
	}
	bool IsMultiline() const
	{
		return bMultiline;
	}

	const FString& GetText() const
	{
		return Text;
	}
	// 文字の数（コードポイントの数）
	int32 GetCharCount() const;
	// カーソルの位置（先頭からのバイト数。文字の境目）
	int32 GetCursorByteOffset() const
	{
		return CursorByteOffset;
	}
	// カーソルの位置（先頭からの文字数）
	int32 GetCursorCharIndex() const;

	// 中身を置き換える（不正な UTF-8 は直し、上限を超える分は切る）。カーソルは末尾へ
	void SetText( const FString& InText );
	void Clear();
	// カーソルを文字の境目へ動かす（範囲の外は端へ寄せる）
	void SetCursorByteOffset( int32 InByteOffset );

	// イベントを 1 つ反映する。文字列かカーソルが変わったら true
	bool Apply( const FTextInputEvent& Event );
	// イベントを順に反映する。1 つでも変わったら true
	bool ApplyAll( const TArray<FTextInputEvent>& Events );

	// 確定（1 行の欄での Enter）が来ていたら true を返して、合図を下ろす
	bool ConsumeSubmitted();
	// 取り消し（Escape）が来ていたら true を返して、合図を下ろす
	bool ConsumeCancelled();

private:
	// 文字列の途中に差し込む。上限を超える分は入れない。入れたら true
	bool InsertAtCursor( const FString& Inserted );
	// ByteOffset の 1 文字前・1 文字後の境目
	int32 PreviousBoundary( int32 ByteOffset ) const;
	int32 NextBoundary( int32 ByteOffset ) const;
	// 行の先頭・末尾の境目（1 行の欄では、文字列の先頭と末尾）
	int32 LineStart( int32 ByteOffset ) const;
	int32 LineEnd( int32 ByteOffset ) const;

	FString Text;
	int32 CursorByteOffset = 0;
	int32 MaxCharCount = 0;
	bool bMultiline = false;
	bool bSubmitted = false;
	bool bCancelled = false;
};
