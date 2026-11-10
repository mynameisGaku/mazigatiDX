// SPDX-License-Identifier: MIT
#include "DX12ImGuiFrameRenderer.h"

FDX12ImGuiFrameRenderer::~FDX12ImGuiFrameRenderer() { Shutdown(); }

bool FDX12ImGuiFrameRenderer::Initialize( FDX12Device& InDevice )
{
	if ( Device || !ImGui::GetCurrentContext() || ImGui::GetIO().BackendRendererUserData || InDevice.IsRecording() || !InDevice.GetNativeDevice() || !InDevice.GetNativeQueue() || !InDevice.GetDescriptorPool() )
	{
		return false;
	}
	Device = &InDevice;
	Context = ImGui::GetCurrentContext();
	if ( !ImGuiRenderer.Initialize( Device->GetNativeDevice(), Device->GetNativeQueue(), 2, DXGI_FORMAT_R8G8B8A8_UNORM, *Device->GetDescriptorPool() ) )
	{
		Shutdown();
		return false;
	}
	return true;
}

bool FDX12ImGuiFrameRenderer::TryShutdown( uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 || ( Device && Device->IsRecording() ) || ( Device && !Device->Wait( TimeoutMs ) && !Device->IsDeviceRemoved() ) )
	{
		return false;
	}
	ImGuiRenderer.Shutdown();
	Context = nullptr;
	Device = nullptr;
	return true;
}

void FDX12ImGuiFrameRenderer::Shutdown()
{
	while ( !TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
}

bool FDX12ImGuiFrameRenderer::BeginFrame()
{
	if ( !Context || Context != ImGui::GetCurrentContext() || !Device || !Device->IsRecording() || !ImGuiRenderer.BeginFrame() )
	{
		return false;
	}
	return true;
}

bool FDX12ImGuiFrameRenderer::Render( ImDrawData* Data )
{
	if ( !Context || Context != ImGui::GetCurrentContext() || !Device || !Device->IsRecording() || !Data )
	{
		return false;
	}
	if ( Data->DisplaySize.x > 0 && Data->DisplaySize.y > 0 )
	{
		Data->FramebufferScale = ImVec2( Device->GetWidth() / Data->DisplaySize.x, Device->GetHeight() / Data->DisplaySize.y );
	}
	if ( !ImGuiRenderer.Render( Data, Device->GetFrameCommands() ) )
	{
		return false;
	}
	return true;
}

FDX12ImGuiRenderer& FDX12ImGuiFrameRenderer::GetImGuiRenderer() { return ImGuiRenderer; }
