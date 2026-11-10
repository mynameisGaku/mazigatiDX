// SPDX-License-Identifier: MIT
#include "DX12GameSurface.h"

#include "Library/Platform/DX12/Device/DX12Device.h"
#include "DX12GameRenderer.h"
#include "DX12GameCapture.h"

CDX12GameSurface::CDX12GameSurface( CDX12Device &InDevice, CDX12GameRenderer &InRenderer, CDX12GameCapture &InCapture ) : Device( InDevice ), Renderer( InRenderer ), Capture( InCapture ) {}
IRenderCanvas2D *CDX12GameSurface::GetCanvas2D() { return Capture.IsActive() ? Capture.GetCanvas2D() : Renderer.GetCanvas2D(); }
IRenderGeometry3D *CDX12GameSurface::GetGeometry3D() { return Capture.IsActive() ? Capture.GetGeometry3D() : Renderer.GetGeometry3D(); }
void CDX12GameSurface::RecordClear( FColor32 Color )
{
	if ( Capture.IsActive() )
	{
		Capture.RecordClear( Color );
	}
	else
	{
		Renderer.RecordClear( Color );
	}
}
bool CDX12GameSurface::WaitForGPU() { return Device.Wait(); }
bool CDX12GameSurface::BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc ) { return Capture.Begin( Desc ); }
bool CDX12GameSurface::EndOffscreenCapture( TArray<FColor32> &Out ) { return Capture.End( Out ); }
