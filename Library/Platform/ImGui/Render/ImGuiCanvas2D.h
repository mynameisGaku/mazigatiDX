// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"

// ImGuiの図形生成だけを再利用する。DX12・Vulkan・OpenGLのGPU型を持たない。
class FImGuiCanvas2D : public IRenderCanvas2D
{
public:
	// 矩形を描く。座標は論理画素。
	void Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness ) override;
	// 線分を描く。
	void Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness ) override;
	// 円を描く。Segmentsの0は自動分割。
	void Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments ) override;
	// 三角形を描く。座標はXYの順で3点。
	void Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness ) override;
	// 凸四角形を描く。座標はXYの順で4点。
	void Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness ) override;
	// 既定フォントでUTF-8の範囲を描く。
	void Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End ) override;
	// 借用画像を凸四角形に貼る。UVは左上と右下。
	void Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color ) override;
	// 現在の範囲との共通部分を積む。空なら描かない。
	void PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 ) override;
	// 直前のクリップ範囲へ戻す。Pushと対にする。
	void PopClip() override;
	// 図形の端の補間を指定する。
	void SetAntiAlias( bool bEnabled ) override;
};
