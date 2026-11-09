// SPDX-License-Identifier: MIT
#include "DX12DrawExecutorSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/Render/Canvas/CanvasDrawExecutor.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CDX12DrawExecutorSubsystem )

bool CDX12DrawExecutorSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CDX12DrawExecutorSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Surface = Collection.InitializeDependency<IRenderSurface>();
	Textures = Collection.InitializeDependency<ITextureDrawSource>();
	Buffers = Collection.InitializeDependency<IGeometryBufferSource>();
}

void CDX12DrawExecutorSubsystem::Deinitialize()
{
	Surface = nullptr;
	Textures = nullptr;
	Buffers = nullptr;
}

void CDX12DrawExecutorSubsystem::Execute( const FDrawSpan& Span )
{
	if ( Surface )
	{
		FCanvasDrawExecutor Executor( *Surface, Textures, Buffers ); // 状態を呼び出し単位で分離する。
		Executor.Execute( Span );
	}
}
