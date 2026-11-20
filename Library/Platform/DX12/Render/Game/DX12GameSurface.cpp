// SPDX-License-Identifier: MIT
#include "DX12GameSurface.h"

#include "Library/Platform/DX12/Device/DX12Device.h"
#include "Library/Platform/DX12/Render/Target/DX12RenderTargetPass.h"
#include "DX12GameRenderer.h"
#include "DX12GameCapture.h"

CDX12GameSurface::CDX12GameSurface( CDX12Device &InDevice, CDX12GameRenderer &InRenderer, CDX12GameCapture &InCapture, CDX12RenderTargetPass *InTargetPass ) : Device( InDevice ), Renderer( InRenderer ), Capture( InCapture ), TargetPass( InTargetPass ) {}
IRenderCanvas2D *CDX12GameSurface::GetCanvas2D()
{
	if ( !Device.IsRecording() )
	{
		return nullptr;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		return TargetPass->GetCanvas2D();
	}
	return Capture.IsActive() ? Capture.GetCanvas2D() : Renderer.GetCanvas2D();
}
IRenderGeometry3D *CDX12GameSurface::GetGeometry3D()
{
	if ( !Device.IsRecording() )
	{
		return nullptr;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		return TargetPass->GetGeometry3D();
	}
	return Capture.IsActive() ? Capture.GetGeometry3D() : Renderer.GetGeometry3D();
}
void CDX12GameSurface::RecordClear( FColor32 Color )
{
	if ( !Device.IsRecording() )
	{
		return;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		TargetPass->RecordClear( Color );
	}
	else if ( Capture.IsActive() )
	{
		Capture.RecordClear( Color );
	}
	else
	{
		Renderer.RecordClear( Color );
	}
}
bool CDX12GameSurface::WaitForGPU() { return Device.Wait(); }
bool CDX12GameSurface::BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc )
{
	return !Capture.IsActive() && ( !TargetPass || !TargetPass->IsActive() ) && Device.IsRecording() && SubmitPendingMainCommands() && Capture.Begin( Desc );
}
bool CDX12GameSurface::EndOffscreenCapture( TArray<FColor32> &Out )
{
	return ( !TargetPass || !TargetPass->IsActive() ) && Capture.End( Out );
}
bool CDX12GameSurface::BeginRenderTargetPass( const FRenderTargetPassDesc &Desc )
{
	return TargetPass && !TargetPass->IsActive() && !Capture.IsActive() && Device.IsRecording() && SubmitPendingMainCommands() && TargetPass->Begin( Desc );
}
bool CDX12GameSurface::CopyColorRegion( int32 X0, int32 Y0, int32 X1, int32 Y1, FTextureResourceHandle Dest )
{
	CDX12TextureStore *Store = Device.GetTextureResources(); // 写し先の画像を調べる所有先。
	FDX12TextureBinding Binding; // 描画中の描画先・深度・形式違いの画像は、ここで外れる。
	if ( !Device.IsRecording() || Capture.IsActive() || !Store || !Store->AcquireGraphicsTexture( Dest, EComputeTextureFormat::RGBA8, Binding ) )
	{
		return false;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		return TargetPass->CopyColorRegion( X0, Y0, X1, Y1, Binding );
	}
	return Device.RecordFrameColorCopy( X0, Y0, X1, Y1, Binding.Resource.Get() ) && Renderer.PinTexture( Binding );
}

bool CDX12GameSurface::RecordTextureCopy( FTextureResourceHandle Source, FTextureResourceHandle Destination )
{
	CDX12TextureStore *Store = Device.GetTextureResources(); // フレームと同じGPUで画像を保持する所有者。
	return Store && !Capture.IsActive() && ( !TargetPass || !TargetPass->IsActive() ) && Store->RecordTextureCopy( Device.GetFrameCommands(), Source, Destination );
}

bool CDX12GameSurface::SubmitPendingMainCommands()
{
	return !Capture.IsActive() && ( !TargetPass || !TargetPass->IsActive() ) && ( !Renderer.GetCanvas2D() || Device.SubmitFrameSegment() );
}
bool CDX12GameSurface::EndRenderTargetPass()
{
	return TargetPass && !Capture.IsActive() && Device.IsRecording() && TargetPass->End();
}

bool CDX12GameSurface::BeginGpuTimestamp( FGpuTimestampHandle Query )
{
	if ( !Device.IsRecording() || Capture.IsActive() )
	{
		return false;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		return TargetPass->BeginGpuTimestamp( Query );
	}
	return Device.BeginMainTimestamp( Query );
}

bool CDX12GameSurface::EndGpuTimestamp( FGpuTimestampHandle Query )
{
	if ( !Device.IsRecording() || Capture.IsActive() )
	{
		return false;
	}
	if ( TargetPass && TargetPass->IsActive() )
	{
		return TargetPass->EndGpuTimestamp( Query );
	}
	return Device.EndMainTimestamp( Query );
}
