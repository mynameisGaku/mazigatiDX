// SPDX-License-Identifier: MIT
#include "NodeGraph.h"

#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float FitMargin = 24.0f;
	constexpr float MinZoom = 0.15f;
	constexpr float MaxZoom = 2.5f;
	// 文字を描かないズームの下限（これより小さいと読めない）
	constexpr float MinTextPixels = 5.0f;

	ImVec2 Plus( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x + B.x, A.y + B.y );
	}

	ImVec2 Minus( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x - B.x, A.y - B.y );
	}

	ImVec2 Scale( const ImVec2& A, float S )
	{
		return ImVec2( A.x * S, A.y * S );
	}

	float Length( const ImVec2& A )
	{
		return NMath::Sqrt( A.x * A.x + A.y * A.y );
	}

	float DistanceToSegment( const ImVec2& P, const ImVec2& A, const ImVec2& B )
	{
		const ImVec2 AB = Minus( B, A );
		const float LengthSq = AB.x * AB.x + AB.y * AB.y;
		float T = LengthSq > 0.0f ? ( ( P.x - A.x ) * AB.x + ( P.y - A.y ) * AB.y ) / LengthSq : 0.0f;
		T = NMath::Clamp( T, 0.0f, 1.0f );
		const ImVec2 Closest = Plus( A, Scale( AB, T ) );
		const float Dx = P.x - Closest.x;
		const float Dy = P.y - Closest.y;
		return NMath::Sqrt( Dx * Dx + Dy * Dy );
	}

}

ImU32 NNodeGraph::Lerp( ImU32 A, ImU32 B, float T )
{
	T = NMath::Clamp( T, 0.0f, 1.0f );
	const ImVec4 ColorA = ImGui::ColorConvertU32ToFloat4( A );
	const ImVec4 ColorB = ImGui::ColorConvertU32ToFloat4( B );
	return ImGui::ColorConvertFloat4ToU32( ImVec4( ColorA.x + ( ColorB.x - ColorA.x ) * T, ColorA.y + ( ColorB.y - ColorA.y ) * T, ColorA.z + ( ColorB.z - ColorA.z ) * T, ColorA.w + ( ColorB.w - ColorA.w ) * T ) );
}

bool FNodeGraphCanvas::Begin( const char* Id, FNodeGraphView& InView, const ImVec2& Size, const ImVec2& ContentSize )
{
	View = &InView;
	CanvasSize = ImVec2( Size.x > 0.0f ? Size.x : ImGui::GetContentRegionAvail().x, Size.y > 0.0f ? Size.y : ImGui::GetContentRegionAvail().y );
	if ( CanvasSize.x < 40.0f || CanvasSize.y < 40.0f )
	{
		return false;
	}

	Origin = ImGui::GetCursorScreenPos();
	ImGui::PushID( Id );
	ImGui::InvisibleButton( "##canvas", CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle );
	bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();

	// 全体が収まるように合わせる（最初と、グラフの大きさが変わったとき）
	if ( !View->bFitted || View->FittedContent.x != ContentSize.x || View->FittedContent.y != ContentSize.y )
	{
		const float ZoomX = CanvasSize.x / NMath::Max( ContentSize.x + FitMargin * 2.0f, 1.0f );
		const float ZoomY = CanvasSize.y / NMath::Max( ContentSize.y + FitMargin * 2.0f, 1.0f );
		View->Zoom = NMath::Clamp( NMath::Min( ZoomX, ZoomY ), MinZoom, 1.2f );
		View->Pan = ImVec2( ( CanvasSize.x / View->Zoom - ContentSize.x ) * 0.5f, ( CanvasSize.y / View->Zoom - ContentSize.y ) * 0.5f );
		View->bFitted = true;
		View->FittedContent = ContentSize;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	// パン: ドラッグ（左か中ボタン）
	if ( bActive && ( ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.0f ) || ImGui::IsMouseDragging( ImGuiMouseButton_Middle, 0.0f ) ) )
	{
		View->Pan = Plus( View->Pan, Scale( IO.MouseDelta, 1.0f / View->Zoom ) );
	}
	// ズーム: ホイール（マウスの下の点を動かさない）
	if ( bHovered && IO.MouseWheel != 0.0f )
	{
		const ImVec2 Mouse = Minus( IO.MousePos, Origin );
		const ImVec2 WorldUnderMouse = Minus( Scale( Mouse, 1.0f / View->Zoom ), View->Pan );
		View->Zoom = NMath::Clamp( View->Zoom * ( 1.0f + IO.MouseWheel * 0.12f ), MinZoom, MaxZoom );
		View->Pan = Minus( Scale( Mouse, 1.0f / View->Zoom ), WorldUnderMouse );
	}

	DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect( Origin, Plus( Origin, CanvasSize ), true );
	DrawList->AddRectFilled( Origin, Plus( Origin, CanvasSize ), IM_COL32( 28, 30, 36, 255 ) );

	// 方眼（ズームに合わせて間隔が変わる）
	const float Step = 48.0f * View->Zoom;
	if ( Step > 8.0f )
	{
		const float OffsetX = NMath::Fmod( View->Pan.x * View->Zoom, Step );
		const float OffsetY = NMath::Fmod( View->Pan.y * View->Zoom, Step );
		for ( float X = OffsetX; X < CanvasSize.x; X += Step )
		{
			DrawList->AddLine( ImVec2( Origin.x + X, Origin.y ), ImVec2( Origin.x + X, Origin.y + CanvasSize.y ), IM_COL32( 255, 255, 255, 10 ) );
		}
		for ( float Y = OffsetY; Y < CanvasSize.y; Y += Step )
		{
			DrawList->AddLine( ImVec2( Origin.x, Origin.y + Y ), ImVec2( Origin.x + CanvasSize.x, Origin.y + Y ), IM_COL32( 255, 255, 255, 10 ) );
		}
	}
	bBegun = true;
	return true;
}

void FNodeGraphCanvas::End()
{
	if ( !bBegun )
	{
		return;
	}
	DrawList->AddRect( Origin, Plus( Origin, CanvasSize ), IM_COL32( 90, 92, 100, 255 ) );
	DrawList->PopClipRect();
	ImGui::PopID();
	bBegun = false;
}

ImVec2 FNodeGraphCanvas::ToScreen( const ImVec2& World ) const
{
	return Plus( Origin, Scale( Plus( World, View->Pan ), View->Zoom ) );
}

bool FNodeGraphCanvas::DrawBox( const ImVec2& WorldMin, const ImVec2& WorldSize, ImU32 Fill, ImU32 Border, float BorderThickness, float Rounding )
{
	const ImVec2 Min = ToScreen( WorldMin );
	const ImVec2 Max = ToScreen( Plus( WorldMin, WorldSize ) );
	const float ScreenRounding = Rounding * View->Zoom;
	DrawList->AddRectFilled( Min, Max, Fill, ScreenRounding );
	DrawList->AddRect( Min, Max, Border, ScreenRounding, 0, BorderThickness * NMath::Max( NMath::Min( View->Zoom, 1.0f ), 0.5f ) );
	return bHovered && ImGui::IsMouseHoveringRect( Min, Max );
}

void FNodeGraphCanvas::DrawBand( const ImVec2& WorldMin, float WorldWidth, float WorldHeight, ImU32 Color, float Rounding )
{
	const ImVec2 Min = ToScreen( WorldMin );
	const ImVec2 Max = ToScreen( Plus( WorldMin, ImVec2( WorldWidth, WorldHeight ) ) );
	DrawList->AddRectFilled( Min, Max, Color, Rounding * View->Zoom, ImDrawFlags_RoundCornersTop );
}

void FNodeGraphCanvas::DrawText( const ImVec2& WorldPos, ImU32 Color, const char* Text, float FontSize, const ImVec2& ClipMin, const ImVec2& ClipSize )
{
	const float Base = FontSize > 0.0f ? FontSize : ImGui::GetFontSize();
	const float Size = Base * View->Zoom;
	if ( Size < MinTextPixels || Text == nullptr )
	{
		return;
	}
	const ImVec2 Pos = ToScreen( WorldPos );
	if ( ClipSize.x > 0.0f )
	{
		const ImVec4 Clip( ToScreen( ClipMin ).x, ToScreen( ClipMin ).y, ToScreen( Plus( ClipMin, ClipSize ) ).x, ToScreen( Plus( ClipMin, ClipSize ) ).y );
		DrawList->AddText( ImGui::GetFont(), Size, Pos, Color, Text, nullptr, 0.0f, &Clip );
	}
	else
	{
		DrawList->AddText( ImGui::GetFont(), Size, Pos, Color, Text );
	}
}

float FNodeGraphCanvas::MeasureText( const char* Text, float FontSize )
{
	const float Base = FontSize > 0.0f ? FontSize : ImGui::GetFontSize();
	return ImGui::GetFont()->CalcTextSizeA( Base, FLT_MAX, 0.0f, Text ).x;
}

bool FNodeGraphCanvas::DrawElbowArrow( const ImVec2* Points, int32 NumPoints, ImU32 Color, float Thickness, float CornerRadius )
{
	if ( Points == nullptr || NumPoints < 2 )
	{
		return false;
	}

	// 画面の座標にして、重なっている点（長さ 0 の線）を除く
	ImVec2 Screen[16];
	int32 Count = 0;
	for ( int32 Index = 0; Index < NumPoints && Count < 16; ++Index )
	{
		const ImVec2 Point = ToScreen( Points[Index] );
		if ( Count == 0 || Length( Minus( Point, Screen[Count - 1] ) ) > 0.5f )
		{
			Screen[Count++] = Point;
		}
	}
	if ( Count < 2 )
	{
		return false;
	}

	// 折れ線（角は、二次曲線で丸める）
	const float ScreenThickness = Thickness * NMath::Max( NMath::Min( View->Zoom, 1.0f ), 0.6f );
	const float Radius = NMath::Clamp( CornerRadius * View->Zoom, 2.0f, 14.0f );
	DrawList->PathClear();
	DrawList->PathLineTo( Screen[0] );
	for ( int32 Index = 1; Index < Count - 1; ++Index )
	{
		const ImVec2 In = Minus( Screen[Index], Screen[Index - 1] );
		const ImVec2 Out = Minus( Screen[Index + 1], Screen[Index] );
		const float InLength = Length( In );
		const float OutLength = Length( Out );
		const float R = NMath::Min( Radius, NMath::Min( InLength, OutLength ) * 0.5f );
		DrawList->PathLineTo( Minus( Screen[Index], Scale( In, R / InLength ) ) );
		DrawList->PathBezierQuadraticCurveTo( Screen[Index], Plus( Screen[Index], Scale( Out, R / OutLength ) ) );
	}
	DrawList->PathLineTo( Screen[Count - 1] );
	DrawList->PathStroke( Color, ImDrawFlags_None, ScreenThickness );

	// 矢じり（最後の線の向きに合わせる）
	ImVec2 Direction = Minus( Screen[Count - 1], Screen[Count - 2] );
	const float EndLength = Length( Direction );
	if ( EndLength > 0.001f )
	{
		Direction = Scale( Direction, 1.0f / EndLength );
		const float Size = NMath::Max( 9.0f * NMath::Min( View->Zoom, 1.3f ), 5.0f );
		const ImVec2 Normal( -Direction.y, Direction.x );
		const ImVec2 Base = Minus( Screen[Count - 1], Scale( Direction, Size ) );
		DrawList->AddTriangleFilled( Screen[Count - 1], Plus( Base, Scale( Normal, Size * 0.5f ) ), Minus( Base, Scale( Normal, Size * 0.5f ) ), Color );
	}

	// マウスが近いか
	if ( !bHovered )
	{
		return false;
	}
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	for ( int32 Index = 1; Index < Count; ++Index )
	{
		if ( DistanceToSegment( Mouse, Screen[Index - 1], Screen[Index] ) < 6.0f )
		{
			return true;
		}
	}
	return false;
}

void FNodeGraphCanvas::DrawDot( const ImVec2& WorldPos, ImU32 Color, float Radius )
{
	DrawList->AddCircleFilled( ToScreen( WorldPos ), NMath::Max( Radius * View->Zoom, 2.0f ), Color );
}

bool FNodeGraphCanvas::DrawRelay( const ImVec2& WorldPos, ImU32 Color, float Radius )
{
	const ImVec2 Center = ToScreen( WorldPos );
	const float ScreenRadius = NMath::Max( Radius * View->Zoom, 2.5f );
	DrawList->AddCircleFilled( Center, ScreenRadius, IM_COL32( 28, 30, 36, 255 ) );
	DrawList->AddCircleFilled( Center, ScreenRadius * 0.62f, Color );
	DrawList->AddCircle( Center, ScreenRadius, Color, 0, 1.6f );
	return bHovered && Length( Minus( ImGui::GetIO().MousePos, Center ) ) < ScreenRadius + 3.0f;
}
