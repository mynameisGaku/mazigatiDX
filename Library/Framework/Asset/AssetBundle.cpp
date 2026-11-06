// SPDX-License-Identifier: MIT
#include "AssetBundle.h"

bool FAssetBundle::AreAllReady() const
{
	for ( const TSharedPtr<CAsset>& Asset : Assets )
	{
		if ( !Asset->IsReady() )
		{
			return false;
		}
	}
	return true;
}
