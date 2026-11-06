// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"

//=============================================================================
// IFontBackend
//   フォント（作成・寸法・文字の形・フォントファイルの登録・環境にあるフォントの調査）を扱う窓口。フォントを使う側（CFontLoader など）が知る
//   すべての関数はメインスレッドだけから呼ばれる（バックエンドがスレッドセーフとは限らない）。実装は IAssetBackend を通して持つ
//=============================================================================
class IFontBackend
{
public:
	virtual ~IFontBackend() = default;

	//-------------------------------------------------------------------------
	// フォント
	//-------------------------------------------------------------------------
	// ※ CreateFont は Windows のマクロ（CreateFontA / W）と衝突するので使わない
	virtual int32 MakeFont( const FFontDesc& Desc ) = 0;
	virtual void DeleteFont( int32 Font ) = 0;
	// 文字列の描画幅（ピクセル）。無効なハンドルなら 0
	virtual int32 MeasureText( int32 Font, const char* Text ) = 0;
	// フォントの寸法。無効なハンドル・非対応の実装は false
	virtual bool GetFontMetrics( int32 /*Font*/, FFontMetrics& /*OutMetrics*/ ) { return false; }
	// 文字列の大きさ（幅・高さ・行数。\n で複数行）。無効なハンドル・非対応の実装は false
	virtual bool MeasureTextSize( int32 /*Font*/, const char* /*Text*/, FTextSize& /*OutSize*/ ) { return false; }
	// 文字 1 つずつの位置と幅（改行も 1 つ）。壊れた UTF-8 は、実装が決めた区切りになる（呼び出す前に、正しい UTF-8 にする）。無効なハンドル・非対応の実装は false
	virtual bool GetTextGlyphs( int32 /*Font*/, const char* /*Text*/, TArray<FTextGlyph>& /*OutGlyphs*/ ) { return false; }
	// その名前のフォントが、この環境にあるか（Face は UTF-8）
	virtual bool IsFontAvailable( const FString& /*Face*/ ) { return false; }
	// この環境にあるフォントの名前の一覧
	virtual void GetFontNames( TArray<FString>& /*OutNames*/ ) {}
	// 指定の 1 文字（Character は UTF-8 の 1 文字）の代わりに、画像 Image を描く（アイコンなどを、文字列の中に入れる）。
	// (DrawX, DrawY) は画像の描く位置のずれ、Advance は次の文字までの幅。登録できたら true
	virtual bool AddFontCharImage( int32 /*Font*/, const char* /*Character*/, int32 /*Image*/, int32 /*DrawX*/, int32 /*DrawY*/, int32 /*Advance*/ ) { return false; }
	virtual void RemoveFontCharImage( int32 /*Font*/, const char* /*Character*/ ) {}
	// フォントファイル（.ttf / .otf）をこのアプリの間だけ使えるようにする／やめる（フォント名で CreateFont できるようになる）
	virtual bool AddFontFile( const FString& Path ) = 0;
	virtual void RemoveFontFile( const FString& Path ) = 0;
};
