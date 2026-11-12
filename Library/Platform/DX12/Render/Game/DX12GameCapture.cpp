// SPDX-License-Identifier: MIT
#include "DX12GameCapture.h"
#include "Library/Platform/DX12/Device/DX12Device.h"
#include "Library/Core/Types/Numeric.h"

CDX12GameCapture::CDX12GameCapture( CDX12Device &InOwner, IRenderTextRasterizer &InText ) : Owner( InOwner ), Text( InText )
{
	bHealthy = Owner.InitializeCaptureTarget( Target );
}

CDX12GameCapture::~CDX12GameCapture()
{
	while ( !Target.TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
	delete Renderer;
}

bool CDX12GameCapture::Begin( const FOffscreenCaptureDesc &Desc )
{
	if ( bActive || !bHealthy || !Owner.IsRecording() || !NMath::IsFinite( Desc.Scale ) || Desc.Scale <= 0 || Desc.Scale > 16384 || !Target.Begin( Desc.Width, Desc.Height, Desc.ClearColor ) )
	{
		return false;
	}
	if ( !Renderer || BoundDepth != Target.GetDepthTexture() )
	{
		delete Renderer;
		Renderer = new CDX12GameRenderer();
		if ( !Renderer->Initialize( Target.GetDevice(), Desc.Width, Desc.Height, *Owner.GetTextureResources(), Text, Target.GetDepthTexture(), Target.GetDepthHeap(), Owner.GetBufferResources(), Owner.GetNativeQueue(), Owner.GetTextureResources(), Owner.GetGraphicsPipelineStore() ) )
		{
			bHealthy = false;
			return false;
		}
		BoundDepth = Target.GetDepthTexture();
	}
	if ( !Renderer->BeginFrame( Target.GetCommands(), Target.GetColorView(), Owner.GetImageDescriptorHeap(), Desc.Scale ) )
	{
		bHealthy = false;
		return false;
	}
	bActive = true;
	return true;
}

bool CDX12GameCapture::End( TArray<FColor32> &Out )
{
	if ( !bActive || !Owner.IsRecording() )
	{
		return false;
	}
	const bool bRendered = Renderer->EndFrame(); // GPUへ記録できなかった文字や図形も撮影失敗へ含める。
	bActive = false;
	if ( !bRendered || !Target.SubmitAndRead( Out ) )
	{
		bHealthy = false;
		return false;
	}
	return true;
}

bool CDX12GameCapture::IsActive() const { return bActive; }
IRenderCanvas2D *CDX12GameCapture::GetCanvas2D() { return bActive ? Renderer->GetCanvas2D() : nullptr; }
IRenderGeometry3D *CDX12GameCapture::GetGeometry3D() { return bActive ? Renderer->GetGeometry3D() : nullptr; }
void CDX12GameCapture::RecordClear( FColor32 Color )
{
	if ( bActive )
	{
		Renderer->RecordClear( Color );
	}
}
bool CDX12GameCapture::WaitForGPU() { return Owner.Wait(); }
