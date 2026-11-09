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

bool FImGuiCanvas2D::SetLogicalSize( int32 Width, int32 Height )
{
	if ( Width <= 0 || Height <= 0 )
	{
		return false;
	}
	LogicalWidth = Width;
	LogicalHeight = Height;
	return true;
}

void FImGuiCanvas2D::TransformVertices( ImDrawList& List, int32 FirstVertex ) const
{
	const ImGuiViewport* Viewport = ImGui::GetMainViewport(); // ゲーム描画は主画面の中へ固定する。
	const float32 ScaleX = LogicalWidth > 0 ? Viewport->Size.x / LogicalWidth : 1; // 横方向の拡大率。
	const float32 ScaleY = LogicalHeight > 0 ? Viewport->Size.y / LogicalHeight : 1; // 縦方向の拡大率。
	for ( int32 Index = FirstVertex; Index < List.VtxBuffer.Size; ++Index )
	{
		ImVec2& Position = List.VtxBuffer[Index].pos; // 色とUVは変えず、今回の頂点位置だけを変換する。
		Position = ImVec2( Viewport->Pos.x + Position.x * ScaleX, Viewport->Pos.y + Position.y * ScaleY );
	}
}

void FImGuiCanvas2D::Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	if ( bFill )
	{
		List->AddRectFilled( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ) );
	}
	else
	{
		List->AddRect( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ), 0, 0, Thickness );
	}
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	List->AddLine( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ), Thickness );
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	if ( bFill )
	{
		List->AddCircleFilled( ImVec2( X, Y ), Radius, ToColor( Color ), Segments );
	}
	else
	{
		List->AddCircle( ImVec2( X, Y ), Radius, ToColor( Color ), Segments, Thickness );
	}
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	if ( bFill )
	{
		List->AddTriangleFilled( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ToColor( Color ) );
	}
	else
	{
		List->AddTriangle( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ToColor( Color ), Thickness );
	}
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	if ( bFill )
	{
		List->AddQuadFilled( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ToColor( Color ) );
	}
	else
	{
		List->AddQuad( ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ToColor( Color ), Thickness );
	}
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End )
{
	SetTextureFilter( true ); // 画像の補間設定を文字へ持ち越さない。
	if ( Begin == End || !Begin || Begin[0] == 0 || Color.a == 0 )
	{
		return;
	}
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const ImGuiViewport* Viewport = ImGui::GetMainViewport(); // 表示範囲も文字の生成前に論理座標へ戻す。
	const float32 ScaleX = LogicalWidth > 0 ? Viewport->Size.x / LogicalWidth : 1; // 横方向の拡大率。
	const float32 ScaleY = LogicalHeight > 0 ? Viewport->Size.y / LogicalHeight : 1; // 縦方向の拡大率。
	if ( !( ScaleX > 0 && ScaleY > 0 ) )
	{
		return;
	}
	const ImVec2 ClipMin = List->GetClipRectMin(), ClipMax = List->GetClipRectMax(); // GPUへ渡す表示座標の範囲は変えない。
	const ImVec4 LogicalClip( ( ClipMin.x - Viewport->Pos.x ) / ScaleX, ( ClipMin.y - Viewport->Pos.y ) / ScaleY, ( ClipMax.x - Viewport->Pos.x ) / ScaleX, ( ClipMax.y - Viewport->Pos.y ) / ScaleY ); // 文字生成時だけ使う論理画素の範囲。
	if ( LogicalClip.x >= LogicalClip.z || LogicalClip.y >= LogicalClip.w )
	{
		return;
	}
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	ImGui::GetFont()->RenderText( List, ImGui::GetFontSize(), ImVec2( X, Y ), ToColor( Color ), LogicalClip, Begin, End );
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	List->AddImageQuad( ImTextureRef( Texture.Binding ), ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ImVec2( UV[0], UV[1] ), ImVec2( UV[2], UV[1] ), ImVec2( UV[2], UV[3] ), ImVec2( UV[0], UV[3] ), ToColor( Color ) );
	TransformVertices( *List, FirstVertex );
}

void FImGuiCanvas2D::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	const ImGuiViewport* Viewport = ImGui::GetMainViewport(); // 頂点と同じ主画面の位置を使う。
	const float32 ScaleX = LogicalWidth > 0 ? Viewport->Size.x / LogicalWidth : 1; // 横方向の拡大率。
	const float32 ScaleY = LogicalHeight > 0 ? Viewport->Size.y / LogicalHeight : 1; // 縦方向の拡大率。
	List->PushClipRect( ImVec2( Viewport->Pos.x + X0 * ScaleX, Viewport->Pos.y + Y0 * ScaleY ), ImVec2( Viewport->Pos.x + X1 * ScaleX, Viewport->Pos.y + Y1 * ScaleY ), true );
}

void FImGuiCanvas2D::PopClip()
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	List->PopClipRect();
}

void FImGuiCanvas2D::SetAntiAlias( bool bEnabled )
{
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // このフレームの2D描画先。
	if ( bEnabled )
	{
		List->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
	}
	else
	{
		List->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill );
	}
}

bool FImGuiCanvas2D::SetTextureFilter( bool bLinear )
{
	const auto Callback = bLinear ? ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear : ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest; // GPU実装が提供する切り替え。
	if ( !Callback )
	{
		return false;
	}
	ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() )->AddCallback( Callback, nullptr );
	return true;
}
