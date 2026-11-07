// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Log/LogCategory.h"

// Null バックエンド（DxLib を使わない実行。プラットフォーム・アセット・描画）のログ（定義は NullPlatformSubsystem.cpp）
DECLARE_LOG_CATEGORY_EXTERN( LogNull, Log )
