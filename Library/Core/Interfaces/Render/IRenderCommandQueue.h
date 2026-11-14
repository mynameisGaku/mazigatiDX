// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/TaskFence.h"
#include "Library/Core/Render/RenderCommandContext.h"
#include "Library/Core/Types/Callable.h"

// ゲーム側から、先行する描画と同じ順序で専用描画スレッドへ仕事を渡す窓口。
class IRenderCommandQueue
{
  public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderCommandQueue() = default;
	// ゲームスレッドで呼ぶ。Taskは値と所有参照を保持し、例外を投げず、ゲームの可変状態・窓・UI文脈へ触れない。
	// Contextは命令内だけ借用し、フレーム・撮影先の管理はゲーム側の窓口で行う。
	// Nameは文字列リテラルかnullptr（既定名）。未起動・空・満杯・終了後・窓側へ貸出中は無効票。票はCPU処理の完了で、GPU完了とは別。
	virtual FTaskFence EnqueueRenderCommand( TCallable<void( const FRenderCommandContext & )> Task, const char *Name = nullptr ) = 0;
};
