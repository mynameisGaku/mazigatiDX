// SPDX-License-Identifier: MIT
#include "GeometryExecution.h"

FGeometryExecution::FGeometryExecution( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels )
	: Textures( InTextures )
	, Models( InModels )
	, Buffers( InBuffers )
	, Geometry( InGeometry )
{
}
