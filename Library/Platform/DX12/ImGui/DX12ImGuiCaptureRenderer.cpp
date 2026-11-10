// SPDX-License-Identifier: MIT
#include "DX12ImGuiCaptureRenderer.h"
#include "DX12ImGuiRenderer.h"
#include "Library/Platform/DX12/Render/DX12Device.h"
#include "Library/Platform/DX12/ImGui/DX12ImGuiGeometryRenderer.h"

FDX12ImGuiCaptureRenderer::FDX12ImGuiCaptureRenderer( FDX12Device& InOwner, FDX12ImGuiRenderer& InRenderer ) : Owner( InOwner )
{
	Renderer = &InRenderer;
	ImageHeap = Owner.GetImageDescriptorHeap();
	bHealthy = Renderer && ImageHeap && Owner.InitializeCaptureTarget( Target );
}

FDX12ImGuiCaptureRenderer::~FDX12ImGuiCaptureRenderer()
{
	while ( !Target.TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
	delete Geometry;
}

bool FDX12ImGuiCaptureRenderer::Begin( const FOffscreenCaptureDesc& Desc )
{
	if ( bActive || !bHealthy || !Owner.IsRecording() || !Recording.Begin( Desc ) )
	{
		return false;
	}
	if ( !Target.Begin( Desc.Width, Desc.Height, Desc.ClearColor ) )
	{
		Recording.Finish();
		return false;
	}
	if ( !Geometry || BoundDepth != Target.GetDepthTexture() )
	{
		delete Geometry;
		Geometry = new FDX12ImGuiGeometryRenderer();
		if ( !Geometry->Initialize( Target.GetDevice(), Desc.Width, Desc.Height, Target.GetDepthTexture(), Target.GetDepthHeap() ) )
		{
			bHealthy = false;
			Recording.Finish();
			return false;
		}
		BoundDepth = Target.GetDepthTexture();
	}
	Target.GetCommands()->SetDescriptorHeaps( 1, &ImageHeap );
	Geometry->BeginFrame( Target.GetCommands(), Target.GetColorView(), ImageHeap, Recording.GetDrawList() );
	bActive = true;
	return true;
}

bool FDX12ImGuiCaptureRenderer::End( TArray<FColor32>& Out )
{
	if ( !bActive || !Owner.IsRecording() || !Recording.IsActive() )
	{
		return false;
	}
	bActive = false;
	ImDrawData* Data = Recording.BuildDrawData(); // 描画データと文字画像の更新はImGui側の責務。
	const bool bRendered = Data && Renderer->Render( Data, Target.GetCommands() ); // API固有の描画は、この接続層からだけ呼ぶ。
	Recording.Finish();
	Geometry->EndFrame();
	if ( !bRendered || !Target.SubmitAndRead( Out ) )
	{
		bHealthy = false;
		return false;
	}
	return true;
}

bool FDX12ImGuiCaptureRenderer::IsActive() const { return bActive; }
IRenderCanvas2D* FDX12ImGuiCaptureRenderer::GetCanvas2D() { return bActive ? Recording.GetCanvas2D() : nullptr; }
IRenderGeometry3D* FDX12ImGuiCaptureRenderer::GetGeometry3D() { return bActive ? Geometry : nullptr; }

void FDX12ImGuiCaptureRenderer::RecordClear( FColor32 Color )
{
	if ( bActive )
	{
		FClearCommand Pending = { this, Color }; // この位置の消去色を所有コピーする。
		Recording.GetDrawList()->AddCallback( ClearCallback, &Pending, sizeof( Pending ) );
		Recording.GetDrawList()->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
	}
}

void FDX12ImGuiCaptureRenderer::ClearCallback( const ImDrawList*, const ImDrawCmd* Command )
{
	const auto& Pending = *static_cast<const FClearCommand*>( Command->UserCallbackData ); // この描画順でGPUへ消去を渡す。
	if ( !Pending.Owner->Target.ClearColor( Pending.Color ) )
	{
		Pending.Owner->bHealthy = false;
	}
}

bool FDX12ImGuiCaptureRenderer::WaitForGPU() { return Owner.Wait(); }
