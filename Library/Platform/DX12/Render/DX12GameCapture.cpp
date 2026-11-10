// SPDX-License-Identifier: MIT
#include "DX12GameCapture.h"
#include "DX12Device.h"
#include "Library/Core/Types/Numeric.h"

FDX12GameCapture::FDX12GameCapture( FDX12Device &InOwner, IRenderTextRasterizer &InText ) : Owner( InOwner ), Text( InText )
{
	bHealthy = Owner.InitializeCaptureTarget( Target );
}

FDX12GameCapture::~FDX12GameCapture()
{
	while ( !Target.TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
	delete Renderer;
}

bool FDX12GameCapture::Begin( const FOffscreenCaptureDesc &Desc )
{
	if ( bActive || !bHealthy || !Owner.IsRecording() || !NMath::IsFinite( Desc.Scale ) || Desc.Scale <= 0 || Desc.Scale > 16384 || !Target.Begin( Desc.Width, Desc.Height, Desc.ClearColor ) )
	{
		return false;
	}
	if ( !Renderer || BoundDepth != Target.GetDepthTexture() )
	{
		delete Renderer;
		Renderer = new FDX12GameRenderer();
		if ( !Renderer->Initialize( Target.GetDevice(), Desc.Width, Desc.Height, *Owner.GetTextureResources(), Text, Target.GetDepthTexture(), Target.GetDepthHeap() ) )
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

bool FDX12GameCapture::End( TArray<FColor32> &Out )
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

bool FDX12GameCapture::IsActive() const { return bActive; }
IRenderCanvas2D *FDX12GameCapture::GetCanvas2D() { return bActive ? Renderer->GetCanvas2D() : nullptr; }
IRenderGeometry3D *FDX12GameCapture::GetGeometry3D() { return bActive ? Renderer->GetGeometry3D() : nullptr; }
void FDX12GameCapture::RecordClear( FColor32 Color )
{
	if ( bActive )
	{
		Renderer->RecordClear( Color );
	}
}
bool FDX12GameCapture::WaitForGPU() { return Owner.Wait(); }
