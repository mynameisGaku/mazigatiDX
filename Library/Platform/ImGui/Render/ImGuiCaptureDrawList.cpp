// SPDX-License-Identifier: MIT
#include "ImGuiCaptureDrawList.h"
#include "Library/Core/Types/Numeric.h"
#include "VendorModules/ImGui/imgui_internal.h"

FImGuiCaptureDrawList::FImGuiCaptureDrawList() : Context( ImGui::GetCurrentContext() )
{
}

FImGuiCaptureDrawList::~FImGuiCaptureDrawList()
{
	delete DrawList;
}

bool FImGuiCaptureDrawList::Begin( const FOffscreenCaptureDesc& Desc )
{
	if ( bActive || !Context || ImGui::GetCurrentContext() != Context || !Context->WithinFrameScope || Desc.Width <= 0 || Desc.Height <= 0 || Desc.Width > 16384 || Desc.Height > 16384 || !NMath::IsFinite( Desc.Scale ) || Desc.Scale <= 0 || Desc.Scale > 16384 )
	{
		return false;
	}
	if ( !DrawList )
	{
		DrawList = new ImDrawList( ImGui::GetDrawListSharedData() );
	}
	DrawList->_ResetForNewFrame();
	DrawList->PushTexture( ImGui::GetFont()->OwnerAtlas->TexRef );
	DrawList->PushClipRect( ImVec2( 0, 0 ), ImVec2( static_cast<float32>( Desc.Width ), static_cast<float32>( Desc.Height ) ) );
	Canvas.SetDrawList( DrawList, Desc.Scale );
	Width = Desc.Width;
	Height = Desc.Height;
	bActive = true;
	return true;
}

ImDrawData* FImGuiCaptureDrawList::BuildDrawData()
{
	if ( !IsActive() )
	{
		return nullptr;
	}
	Data.Clear();
	Data.Valid = true;
	Data.DisplayPos = ImVec2( 0, 0 );
	Data.DisplaySize = ImVec2( static_cast<float32>( Width ), static_cast<float32>( Height ) );
	Data.FramebufferScale = ImVec2( 1, 1 );
	Data.OwnerViewport = ImGui::GetMainViewport();
	Textures.resize( 0 );
	for ( const ImDrawCmd& Command : DrawList->CmdBuffer )
	{
		ImTextureData* Texture = Command.TexRef._TexData; // 通常のEndFrame前に、参照する文字画像の更新を渡す。
		if ( Texture && ( Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates ) && !Textures.contains( Texture ) )
		{
			Textures.push_back( Texture );
		}
	}
	Data.Textures = &Textures;
	Data.AddDrawList( DrawList );
	return &Data;
}

void FImGuiCaptureDrawList::Finish()
{
	bActive = false;
	Canvas.SetDrawList( nullptr );
}

bool FImGuiCaptureDrawList::IsActive() const { return bActive && ImGui::GetCurrentContext() == Context; }
IRenderCanvas2D* FImGuiCaptureDrawList::GetCanvas2D() { return IsActive() ? &Canvas : nullptr; }
ImDrawList* FImGuiCaptureDrawList::GetDrawList() { return IsActive() ? DrawList : nullptr; }
