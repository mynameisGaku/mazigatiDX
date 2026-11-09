// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawSpan.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"

// 共通命令を2Dキャンバスと基本3Dへ振り分ける。状態はExecuteごとに初期化する。
class FCanvasDrawExecutor
{
public:
	// SurfaceとTexturesは借用。実行が終わるまで所有側が生存させる。
	FCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers = nullptr, IStaticModelSource* InModels = nullptr );
	// 命令の長さを検査して実行する。未対応命令以降はそのSpan内で省略する。
	void Execute( const FDrawSpan& Span );
private:
	IRenderSurface& Surface; // 借用する描画先。
	IStaticModelSource* Models; // 同期実行中だけ借りる静的モデル。
	IGeometryBufferSource* Buffers; // 同期の命令解釈にだけ借用する形状。
	ITextureDrawSource* Textures; // 借用する登録画像。nullptrなら画像を描かない。
};
