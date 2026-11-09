// SPDX-License-Identifier: MIT
#include "ImGuiCanvas2D.h"
#include "VendorModules/ImGui/imgui.h"

namespace
{
	// 共通のRGBAをImGuiのパック値へ変換する。
	ImU32 ToColor( FColor32 Color )
	{
		return IM_COL32( Color.r, Color.g, Color.b, Color.a );
	}
}

void FImGuiCanvas2D::Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	if ( bFill )
	{
		List->AddRectFilled( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ) );
	}
	else
	{
		List->AddRect( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ), 0, 0, Thickness );
	}
}

void FImGuiCanvas2D::Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	List->AddLine( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ), Thickness );
}

void FImGuiCanvas2D::Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	if ( bFill )
	{
		List->AddCircleFilled( ImVec2( X, Y ), Radius, ToColor( Color ), Segments );
	}
	else
	{
		List->AddCircle( ImVec2( X, Y ), Radius, ToColor( Color ), Segments, Thickness );
	}
}

void FImGuiCanvas2D::Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	if ( bFill )
	{
		List->AddTriangleFilled( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ToColor( Color ) );
	}
	else
	{
		List->AddTriangle( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ToColor( Color ), Thickness );
	}
}

void FImGuiCanvas2D::Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	if ( bFill )
	{
		List->AddQuadFilled( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ToColor( Color ) );
	}
	else
	{
		List->AddQuad( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ToColor( Color ), Thickness );
	}
}

void FImGuiCanvas2D::Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	List->AddText( ImVec2( X, Y ), ToColor( Color ), Begin, End );
}

void FImGuiCanvas2D::Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	List->AddImageQuad( ImTextureRef( Texture.Binding ), ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ImVec2( UV[0], UV[1] ), ImVec2( UV[2], UV[1] ), ImVec2( UV[2], UV[3] ), ImVec2( UV[0], UV[3] ), ToColor( Color ) );
}

void FImGuiCanvas2D::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	List->PushClipRect( ImVec2( static_cast<float32>( X0 ), static_cast<float32>( Y0 ) ), ImVec2( static_cast<float32>( X1 ), static_cast<float32>( Y1 ) ), true );
}

void FImGuiCanvas2D::PopClip()
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	List->PopClipRect();
}

void FImGuiCanvas2D::SetAntiAlias( bool bEnabled )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList(); // このフレームの2D描画先。
	if ( bEnabled )
	{
		List->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
	}
	else
	{
		List->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill );
	}
}
