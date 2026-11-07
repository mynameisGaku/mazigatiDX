// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Log/LogCategory.h"

// DxLib の描画の実行（描画コマンドを DxLib の呼び出しにする所。モデルの材質・コマ姿勢・エフェクト材質の実行を含む）のログ（定義は DxLibDrawExecutorSubsystem.cpp）
DECLARE_LOG_CATEGORY_EXTERN( LogDxLibDraw, Log )
