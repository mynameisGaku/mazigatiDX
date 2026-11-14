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

bool CImGuiCanvas2D::SetLogicalSize( int32 Width, int32 Height )
{
	if ( Width <= 0 || Height <= 0 )
	{
		return false;
	}
	LogicalWidth = Width;
	LogicalHeight = Height;
	return true;
}

void CImGuiCanvas2D::SetDrawList( ImDrawList* List, float32 Scale )
{
	DrawList = List;
	DrawScale = Scale;
}

ImDrawList* CImGuiCanvas2D::GetDrawList() const
{
	return DrawList ? DrawList : ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() );
}

ImVec4 CImGuiCanvas2D::GetTransform() const
{
	if ( DrawList )
	{
		return ImVec4( 0, 0, DrawScale, DrawScale );
	}
	const ImGuiViewport* Viewport = ImGui::GetMainViewport(); // 通常描画は主画面の中へ固定する。
	return ImVec4( Viewport->Pos.x, Viewport->Pos.y, LogicalWidth > 0 ? Viewport->Size.x / LogicalWidth : 1, LogicalHeight > 0 ? Viewport->Size.y / LogicalHeight : 1 );
}

void CImGuiCanvas2D::TransformVertices( ImDrawList& List, int32 FirstVertex ) const
{
	const ImVec4 Transform = GetTransform(); // 撮影先と主画面で同じ変換を使う。
	for ( int32 Index = FirstVertex; Index < List.VtxBuffer.Size; ++Index )
	{
		ImVec2& Position = List.VtxBuffer[Index].pos; // 色とUVは変えず、今回の頂点位置だけを変換する。
		Position = ImVec2( Transform.x + Position.x * Transform.z, Transform.y + Position.y * Transform.w );
	}
}

void CImGuiCanvas2D::Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
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

void CImGuiCanvas2D::Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	List->AddLine( ImVec2( X0, Y0 ), ImVec2( X1, Y1 ), ToColor( Color ), Thickness );
	TransformVertices( *List, FirstVertex );
}

void CImGuiCanvas2D::Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
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

void CImGuiCanvas2D::Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
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

void CImGuiCanvas2D::Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
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

void CImGuiCanvas2D::Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End )
{
	SetTextureFilter( true ); // 画像の補間設定を文字へ持ち越さない。
	if ( Begin == End || !Begin || Begin[0] == 0 || Color.a == 0 )
	{
		return;
	}
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	const ImVec4 Transform = GetTransform(); // 表示範囲も文字の生成前に論理座標へ戻す。
	const float32 ScaleX = Transform.z, ScaleY = Transform.w; // 撮影倍率または主画面への拡大率。
	if ( !( ScaleX > 0 && ScaleY > 0 ) )
	{
		return;
	}
	const ImVec2 ClipMin = List->GetClipRectMin(), ClipMax = List->GetClipRectMax(); // GPUへ渡す表示座標の範囲は変えない。
	const ImVec4 LogicalClip( ( ClipMin.x - Transform.x ) / ScaleX, ( ClipMin.y - Transform.y ) / ScaleY, ( ClipMax.x - Transform.x ) / ScaleX, ( ClipMax.y - Transform.y ) / ScaleY ); // 文字生成時だけ使う論理画素の範囲。
	if ( LogicalClip.x >= LogicalClip.z || LogicalClip.y >= LogicalClip.w )
	{
		return;
	}
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	ImGui::GetFont()->RenderText( List, ImGui::GetFontSize(), ImVec2( X, Y ), ToColor( Color ), LogicalClip, Begin, End );
	TransformVertices( *List, FirstVertex );
}

void CImGuiCanvas2D::Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	const int32 FirstVertex = List->VtxBuffer.Size; // 今回追加する頂点の先頭。
	List->AddImageQuad( ImTextureRef( Texture.Binding ), ImVec2( Points[0], Points[1] ), ImVec2( Points[2], Points[3] ), ImVec2( Points[4], Points[5] ), ImVec2( Points[6], Points[7] ), ImVec2( UV[0], UV[1] ), ImVec2( UV[2], UV[1] ), ImVec2( UV[2], UV[3] ), ImVec2( UV[0], UV[3] ), ToColor( Color ) );
	TransformVertices( *List, FirstVertex );
}

void CImGuiCanvas2D::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	const ImVec4 Transform = GetTransform(); // 頂点と同じ原点と倍率を使う。
	List->PushClipRect( ImVec2( Transform.x + X0 * Transform.z, Transform.y + Y0 * Transform.w ), ImVec2( Transform.x + X1 * Transform.z, Transform.y + Y1 * Transform.w ), true );
}

void CImGuiCanvas2D::PopClip()
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	List->PopClipRect();
}

void CImGuiCanvas2D::SetAntiAlias( bool bEnabled )
{
	ImDrawList* List = GetDrawList(); // このフレームの2D描画先。
	if ( bEnabled )
	{
		List->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
	}
	else
	{
		List->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill );
	}
}

bool CImGuiCanvas2D::SetTextureFilter( bool bLinear )
{
	const auto Callback = bLinear ? ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear : ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest; // GPU実装が提供する切り替え。
	if ( !Callback )
	{
		return false;
	}
	GetDrawList()->AddCallback( Callback, nullptr );
	return true;
}
