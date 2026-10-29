// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/FontTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// テキストレイアウト（折り返し）
//   文字列を、1 行の最大の幅に収まるように、行に分ける。DxLib にも描画にも依存しない純粋な計算（文字の幅は ITextMeasurer から得る）。
//   CFont::Layout / CFont::RecordWrapped が使う。
//
//   折り返しの規則:
//     - 改行（LF）で必ず行を分ける（CR は無視する）。空の行も 1 行
//     - 空白（スペース・タブ・全角スペース）のあとで折る。行末にぶら下がった空白は、幅に数えず、行に含めない。折り返したあとの行頭の空白は読み飛ばす
//     - 漢字・かな・ハングル・全角文字は、1 文字ごとに折れる（英単語は、途中では折らない）
//     - 長すぎる単語（1 行に収まらない）は、文字の途中で折る（行頭に置けない句読点などは、行の端にはみ出して、前の文字にぶら下げる）
//     - bAvoidPunctuationAtLineEdges が true なら、句読点・閉じ括弧・小さいかな・長音符「ー」などを行頭に置かず（前の文字ごと次の行へ送る）、開き括弧を行末に置かない
//   MaxWidth より広い文字 1 つは、その文字だけで 1 行になる（切らない）。
//=============================================================================

// 折り返しの設定
struct FTextLayoutOptions
{
	int32 MaxWidth = 0;                       // 1 行の最大の幅（ピクセル）。0 以下なら、折り返さない（改行だけで分ける）
	int32 MaxLines = 0;                       // 行数の上限。0 は無制限。超える分は捨てて、bTruncated を true にする
	bool bAvoidPunctuationAtLineEdges = true; // 句読点などを行頭に、開き括弧を行末に置かないか（日本語の組版の、いわゆる禁則処理）
};

// 行 1 つ（Text の中の範囲）
struct FTextLine
{
	int32 Begin = 0;   // 行の先頭のバイト位置
	int32 End = 0;     // 行の終わりのバイト位置（含まない。改行・折り返しの空白は含まない）
	int32 Width = 0;   // 行の幅（ピクセル。末尾の空白を除く）
};

// 配置した結果
struct FTextLayout
{
	FString Text;                // 配置した文字列（壊れた UTF-8 は、正しい形に直したもの。Lines は、この文字列の中の範囲）
	TArray<FTextLine> Lines;     // 行（上から順）。空の文字列は 0 行
	int32 Width = 0;             // いちばん長い行の幅
	bool bTruncated = false;     // MaxLines で、残りを捨てたか

	// 行 Index の文字列（範囲の外は空）
	FString GetLineText( int32 Index ) const;
};

// 文字の幅を教える窓口（CFont がバックエンド経由で実装する。確認では、決まった幅を返す偽の実装を使う）
class ITextMeasurer
{
public:
	virtual ~ITextMeasurer() = default;

	// Text（Length バイト。正しい UTF-8）の文字 1 つずつの位置と幅を返す。改行も 1 つ（幅 0）。取れなければ false
	virtual bool GetGlyphs( const char* Text, int32 Length, TArray<FTextGlyph>& OutGlyphs ) const = 0;
};

namespace NTextLayout
{
	// 文字列を、Options の通りに行に分ける。Measurer が文字を測れなければ、行なし（Lines が空）
	FTextLayout Build( const char* Text, const FTextLayoutOptions& Options, const ITextMeasurer& Measurer );

	// UTF-8 の Text（Length バイト）の InOutOffset から、コードポイントを 1 つ読んで、InOutOffset を進める。
	// 壊れた並びは、1 バイトを U+FFFD として進む。InOutOffset が範囲の外なら 0 を返す（進めない）
	uint32 DecodeUtf8( const char* Text, int32 Length, int32& InOutOffset );

	// 空白か（スペース・タブ・全角スペース・CR）。折り返しのとき、行末にぶら下がる
	bool IsWhitespace( uint32 CodePoint );

	// どこででも折れる文字か（漢字・かな・ハングル・全角文字など。英単語のように、単語の区切りでしか折れない文字ではない）
	bool IsBreakableAnywhere( uint32 CodePoint );

	// 行頭に置いてよい文字か（句読点・閉じ括弧・小さいかな・長音符などは、置かない方がよい）
	bool CanStartLine( uint32 CodePoint );

	// 行末に置いてよい文字か（開き括弧などは、置かない方がよい）
	bool CanEndLine( uint32 CodePoint );

	// 文字 Before と After のあいだで、行を折ってよいか
	bool CanBreakBetween( uint32 Before, uint32 After, bool bAvoidPunctuationAtLineEdges );
}
