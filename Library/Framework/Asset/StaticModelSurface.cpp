// SPDX-License-Identifier: NOASSERTION
#include "StaticModelSurface.h"

#include "Library/Framework/Asset/Model.h"
#include <utility>

FStaticModelSurface::FStaticModelSurface( const TSharedPtr<const CModel>& InSource, FStaticModelSurfaceData&& InData )
	: Source( InSource )
	, Data( std::move( InData ) )
{
}
