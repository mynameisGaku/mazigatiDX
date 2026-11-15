// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawSpan.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Interfaces/Render/IFontDrawSource.h"
#include "Library/Core/Interfaces/Render/IRenderTargetDrawSource.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"

// 共通命令を2Dキャンバスと基本3Dへ振り分ける。状態はExecuteごとに初期化する。
class CCanvasDrawExecutor
{
public:
	// SurfaceとTexturesは借用。実行が終わるまで所有側が生存させる。
	// Fontsがnullptrなら、文字は実行先の既定の文字で描く（フォント・位置揃え・拡大・回転なし）。Targetsがnullptrなら、描画先へ描く範囲は描かない。
	CCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers = nullptr, IStaticModelSource* InModels = nullptr, IFontDrawSource* InFonts = nullptr, IRenderTargetDrawSource* InTargets = nullptr, IShaderDrawSource* InShaders = nullptr );
	// 命令の長さを検査して実行する。未対応命令以降はそのSpan内で省略する。
	void Execute( const FDrawSpan& Span );
private:
	IRenderSurface& Surface; // 借用する描画先。
	IStaticModelSource* Models; // 同期実行中だけ借りる静的モデル。
	IGeometryBufferSource* Buffers; // 同期の命令解釈にだけ借用する形状。
	ITextureDrawSource* Textures; // 借用する登録画像。nullptrなら画像を描かない。
	IFontDrawSource* Fonts; // 借用するフォントの表。nullptrなら既定の文字で描く。
	IRenderTargetDrawSource* Targets; // 借用する描画先の表。nullptrなら描画先へ描けない。
	IShaderDrawSource* Shaders; // 借用するシェーダーの実行先。nullptrならシェーダーの命令は飛ばす。
};
