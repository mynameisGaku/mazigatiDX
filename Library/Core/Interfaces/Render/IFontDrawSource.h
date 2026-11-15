// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/RenderTextFont.h"
#include "Library/Framework/Asset/Font/FontTypes.h"

// 文字の代わりに入れる画像の置き場所（文字列の左上からの画素）。Image は、画像の番号。
struct FFontImagePlacement
{
	int32 Image = -1;
	int32 X = 0, Y = 0;
};

// フォントの所有・作成から、描画側を切り離す。描画の命令を解釈する側が、フォントの番号を、描画に使う値と大きさへ変える。
// ゲームスレッドから呼ぶ。
class IFontDrawSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IFontDrawSource() = default;
	// フォントの番号を、文字を画像にするための値にする。負の番号は、既定のフォント。無効な番号は false（出力を保つ）。
	virtual bool GetFontDrawView( int32 Font, FRenderTextFont& Out ) = 0;
	// Text の先頭から Length バイト（UTF-8）を描いたときの大きさ。無効な番号・壊れた文字列は false（出力を保つ）。
	virtual bool MeasureFontText( int32 Font, const char* Text, int32 Length, FTextSize& Out ) = 0;
	// Text の先頭から Length バイトの中で、文字の代わりに画像を入れる文字（AddFontCharImage）の、画像の置き場所を返す。入れる文字が無い・非対応は false（出力を保つ）。
	virtual bool GetFontImagePlacements( int32 /*Font*/, const char* /*Text*/, int32 /*Length*/, TArray<FFontImagePlacement>& /*Out*/ ) { return false; }
};
