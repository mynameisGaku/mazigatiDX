// SPDX-License-Identifier: MIT
#include "DX12DrawExecutorSubsystem.h"
#include "Library/Core/Render/RenderLook.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/Render/Canvas/CanvasDrawExecutor.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/System/CommandLine.h"

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
	Models = Collection.InitializeDependency<IStaticModelSource>();
	Fonts = Collection.InitializeDependency<IFontDrawSource>();
	Targets = Collection.InitializeDependency<IRenderTargetDrawSource>();
	Shaders = Collection.InitializeDependency<IShaderDrawSource>();
	// 見た目の水準（--dx12-look=compat | standard。DX12 の本体と同じ読み方で、読めなければ compat）。compat は、DxLib に合わせる。
	ERenderLook Look = ERenderLook::Compat;
	if ( const TOptional<FString> LookOption = NCommandLine::GetValue( "dx12-look" ); LookOption.IsSet() && !NRenderLook::TryParse( LookOption.GetValue().GetData(), Look ) )
	{
		Look = ERenderLook::Compat;
	}
	bSolidShapeAlphaSquared = Look == ERenderLook::Compat;
}

void CDX12DrawExecutorSubsystem::Deinitialize()
{
	Surface = nullptr;
	Textures = nullptr;
	Buffers = nullptr;
	Models = nullptr;
	Fonts = nullptr;
	Targets = nullptr;
	Shaders = nullptr;
}

void CDX12DrawExecutorSubsystem::Execute( const FDrawSpan& Span )
{
	if ( Surface )
	{
		CCanvasDrawExecutor Executor( *Surface, Textures, Buffers, Models, Fonts, Targets, Shaders ); // 状態を呼び出し単位で分離する。
		Executor.SetSolidShapeAlphaSquared( bSolidShapeAlphaSquared );
		Executor.Execute( Span );
	}
}

bool CDX12DrawExecutorSubsystem::BeginOffscreenCapture( const FOffscreenCaptureDesc& Desc )
{
	return Surface && Surface->BeginOffscreenCapture( Desc );
}

bool CDX12DrawExecutorSubsystem::EndOffscreenCapture( TArray<FColor32>& Out )
{
	return Surface && Surface->EndOffscreenCapture( Out );
}
