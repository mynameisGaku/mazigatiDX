// SPDX-License-Identifier: NOASSERTION
#include "StaticModelSurface.h"

#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/Model.h"

FStaticModelSurface::FStaticModelSurface( const TSharedPtr<const CModel>& InSource, FStaticModelSurfaceData&& InData )
	: Source( InSource )
	, Data( MoveTemp( InData ) )
{
}
