// SPDX-License-Identifier: MIT
#include "Library/Core/Log/Log.h"

#ifdef _DEBUG
DEFINE_LOG_CATEGORY_STATIC( LogImGuiAssert, Log )

// ImGui の IM_ASSERT から呼ばれる（VendorModules/ImGui/imconfig.h）。原因をログに残してから止める。
// どのプラットフォームの実装（DxLib・Null）でも ImGui を使うので、特定の実装の外に置く
void CoGImGuiAssertFailed( const char* Expression, const char* File, int Line )
{
	LOG( LogImGuiAssert, Fatal, "ImGui assertion failed: %s (%s:%d)", Expression, File, Line );
}
#endif
