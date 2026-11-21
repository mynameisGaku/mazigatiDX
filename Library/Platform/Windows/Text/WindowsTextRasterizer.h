// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextRasterizer.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Platform/Windows/Text/WindowsGdiText.h"

// Windowsの既定ゲーム文字をCPU画像へ変換する。GPUとImGuiは所有しない。
class CWindowsTextRasterizer : public IRenderTextRasterizer
{
  public:
	// OSの描画文脈とフォントの所有は複写しない。
	CWindowsTextRasterizer() = default;
	CWindowsTextRasterizer( const CWindowsTextRasterizer & ) = delete;
	CWindowsTextRasterizer &operator=( const CWindowsTextRasterizer & ) = delete;
	// 選択したOS資源を戻して破棄する。
	~CWindowsTextRasterizer();
	// UI設定から独立した13画素の等幅文字を用意する。OS資源不足ではfalse。
	bool Initialize();
	// UTF-8を白いRGBAへ変換する。空・不正・64KiB超は全出力を保ってfalse。各辺4096・4M画素を超える文字列は、はみ出す右・下を切った画像にする（DxLibが画面の端で切るのと同じ）。
	bool RasterizeText( TArrayView<const char> Text, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight ) override;
	// フォントを指定した文字列を、白いRGBA（と、縁取りがあればその画像）へ変換する。失敗時は全出力を保ってfalse。
	bool RasterizeFontText( const FRenderTextFont &InFont, TArrayView<const char> Text, TArray<FColor32> &OutPixels, TArray<FColor32> &OutEdgePixels, int32 &OutWidth, int32 &OutHeight ) override;

  private:
	CWindowsGdiText FontText; // フォントを指定した文字の画像化。この実体を使うスレッドだけが触る。
	HDC Context = nullptr; // CPU上の文字画像を作るOSの文脈。
	HFONT Font = nullptr; // UIに依存しない既定のゲームフォント。
	HGDIOBJ PreviousFont = nullptr; // 破棄時に戻す元の選択。
};
