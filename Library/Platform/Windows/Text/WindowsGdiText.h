// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Render/RenderTextFont.h"
#include "Library/Framework/Asset/Font/FontTypes.h"
#include "Library/Platform/System/WindowsInclude.h"

//=============================================================================
// CWindowsGdiText
//   Windows の文字（GDI）で、フォントを指定した文字列を、測って、CPU の画像にする。GPU・UI・ハンドルの表を持たない。
//   1 つの実体は、1 つのスレッドだけから使う（測る側と、画像にする側は、別々の実体を持つ。どちらも同じ計算なので、大きさが合う）。
//
//   並べ方: 文字（UTF-8 のコードポイント）を 1 つずつ左から置く。1 文字の幅 ＝ OS の送り幅 ＋ 字間 ＋ 縁取り x 2。改行で、次の行の左端へ。
//           1 行の文字の高さ ＝ フォントの高さ ＋ 2（上下の余白）＋ 縁取り x 2。太さは、6 以下が標準、7〜9 で太くなる。行の送り幅は、指定が無ければ、1 行の文字の高さ。
//=============================================================================
class CWindowsGdiText
{
public:
	// OS の描画文脈とフォントの所有は複写しない。
	CWindowsGdiText() = default;
	CWindowsGdiText( const CWindowsGdiText& ) = delete;
	CWindowsGdiText& operator=( const CWindowsGdiText& ) = delete;
	// 作ったフォントと文脈を返す。
	~CWindowsGdiText();

	// フォントの寸法。OS の資源を作れなければ false（出力は変えない）
	bool GetMetrics( const FRenderTextFont& Font, FFontMetrics& OutMetrics );
	// 文字列の大きさと、文字ごとの位置（OutGlyphs は省略できる）。壊れた UTF-8・64KiB 超は false（出力は変えない）。空の文字列は、大きさ 0 で成功
	bool Layout( const FRenderTextFont& Font, TArrayView<const char> Text, FTextSize& OutSize, TArray<FTextGlyph>* OutGlyphs = nullptr );
	// 文字列を、被覆率（0..255）の画像にする。大きさは Layout と同じ。縁取りがあれば、OutEdge に、縁取りを含む被覆率も返す（無ければ空）。
	// 空・壊れた UTF-8・各辺 4096 超・4M 画素超は false（出力は変えない）
	bool Rasterize( const FRenderTextFont& Font, TArrayView<const char> Text, TArray<uint8>& OutCoverage, TArray<uint8>& OutEdge, int32& OutWidth, int32& OutHeight );

private:
	// 1 文字（UTF-16 で 1 つか 2 つ）と、文字列の中の位置
	struct FCharacter
	{
		wchar_t Units[2] = {};
		int32 NumUnits = 0;
		int32 Offset = 0;   // UTF-8 のバイト位置
		int32 Bytes = 0;    // UTF-8 のバイト数
		int32 X = 0, Y = 0; // 置く位置（縁取りを含まない、文字の左上）
		int32 Width = 0;    // 次の文字までの幅
		bool bNewLine = false;
	};

	// 文脈を用意する。作れなければ false
	bool EnsureContext();
	// 指定のフォントを作って（作ってあれば、それを）文脈へ選ぶ。作れなければ false
	bool SelectFont( const FRenderTextFont& Font );
	// 文字を 1 つずつに分けて、置く位置を決める。フォントは選んであること
	bool Arrange( const FRenderTextFont& Font, TArrayView<const char> Text, TArray<FCharacter>& OutCharacters, FTextSize& OutSize );

	HDC Context = nullptr;            // CPU 上の文字画像を作る OS の文脈
	HGDIOBJ PreviousFont = nullptr;   // 破棄するときに戻す、元の選択
	TMap<FString, HFONT> Fonts;       // 指定ごとに作ったフォント（鍵は FRenderTextFont::ToKey）
};
