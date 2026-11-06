// SPDX-License-Identifier: MIT
#include "Library/DebugUI/Common/ImGuiTexture.h"

namespace
{
	NDebugUI::FImGuiTextureCallbacks GCallbacks;
}

void NDebugUI::SetImGuiTextureCallbacks( const FImGuiTextureCallbacks& Callbacks )
{
	GCallbacks = Callbacks;
}

ImTextureID NDebugUI::GetImGuiTexture( int32 GraphHandle )
{
	return GCallbacks.GetTexture != nullptr ? GCallbacks.GetTexture( GraphHandle ) : 0;
}

void NDebugUI::TickImGuiTextureCache()
{
	if ( GCallbacks.TickCache != nullptr )
	{
		GCallbacks.TickCache();
	}
}

void NDebugUI::ClearImGuiTextureCache()
{
	if ( GCallbacks.ClearCache != nullptr )
	{
		GCallbacks.ClearCache();
	}
}

int32 NDebugUI::GetImGuiTextureCacheSize()
{
	return GCallbacks.GetCacheSize != nullptr ? GCallbacks.GetCacheSize() : 0;
}

void NDebugUI::BeginNearest( ImDrawList* DrawList )
{
	if ( GCallbacks.BeginNearest != nullptr )
	{
		GCallbacks.BeginNearest( DrawList );
	}
}

void NDebugUI::EndNearest( ImDrawList* DrawList )
{
	if ( GCallbacks.EndNearest != nullptr )
	{
		GCallbacks.EndNearest( DrawList );
	}
}
