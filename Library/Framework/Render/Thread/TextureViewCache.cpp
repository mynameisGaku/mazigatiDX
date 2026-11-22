// SPDX-License-Identifier: MIT
#include "TextureViewCache.h"

bool CTextureViewCache::Find( FTextureResourceHandle Texture, FTextureDrawView &OutView ) const
{
	const FTextureDrawView *Found = Views.Find( Texture.Value );
	if ( !Found )
	{
		return false;
	}
	OutView = *Found;
	return true;
}

void CTextureViewCache::Add( FTextureResourceHandle Texture, const FTextureDrawView &View )
{
	if ( !Texture.IsValid() )
	{
		return;
	}
	if ( Views.Num() >= MaxViews )
	{
		Views.Reset();
	}
	Views.Add( Texture.Value, View );
}

void CTextureViewCache::Invalidate()
{
	Views.Reset();
}
