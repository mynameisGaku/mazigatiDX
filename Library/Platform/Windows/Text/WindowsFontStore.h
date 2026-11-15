// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"
#include "Library/Core/Interfaces/Render/IFontDrawSource.h"
#include "Library/Platform/Windows/Text/WindowsGdiText.h"

//=============================================================================
// CWindowsFontStore
//   フォントの番号の表。作ったフォントの指定（値）を番号で覚えて、大きさを測る。描画 API と GPU を知らない。
//   文字を画像にするのは、描画側（IRenderTextRasterizer）。ここは、番号から値（FRenderTextFont）を渡すだけ。
//   ゲームスレッドだけから使う。
//=============================================================================
class CWindowsFontStore : public IFontDrawSource
{
public:
	// フォントを登録して、番号を返す。使えない指定（大きさが 1 未満など）は NAsset::InvalidHandle
	int32 MakeFont( const FFontDesc& Desc );
	// 番号を空ける。無効な番号は何もしない
	void DeleteFont( int32 Font );
	// 文字列（'\0' まで）の幅。無効な番号は 0
	int32 MeasureText( int32 Font, const char* Text );
	bool GetFontMetrics( int32 Font, FFontMetrics& OutMetrics );
	bool MeasureTextSize( int32 Font, const char* Text, FTextSize& OutSize );
	bool GetTextGlyphs( int32 Font, const char* Text, TArray<FTextGlyph>& OutGlyphs );
	// その名前のフォントが、この環境にあるか
	bool IsFontAvailable( const FString& Face );
	// この環境にあるフォントの名前（重複なし・名前の順）
	void GetFontNames( TArray<FString>& OutNames );
	// フォントファイルを、このアプリの間だけ使えるようにする／やめる
	bool AddFontFile( const FString& Path );
	void RemoveFontFile( const FString& Path );
	// 登録してあるフォントの数
	int32 GetNumFonts() const;

	bool GetFontDrawView( int32 Font, FRenderTextFont& Out ) override;
	bool MeasureFontText( int32 Font, const char* Text, int32 Length, FTextSize& Out ) override;

private:
	struct FEntry
	{
		FRenderTextFont Font;
		bool bUsed = false;
	};

	TArray<FEntry> Entries;      // 番号 = 配列の位置
	TArray<FString> FontFiles;   // 足したフォントファイル
	CWindowsGdiText Text;        // 測るための、ゲームスレッド用の文字
};
