// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Framework/Render/Camera/Camera.h"

// 四角形の向き・基準点・UVを三角形へ展開する。画像やGPU資源は取得しない。
class CGeometryQuadBuilder
{
public:
	// Fixedは指定軸、Cameraは画面と平行、CameraYはY軸を保つ。
	// 幅または高さ0なら空で成功。不正値ではOutを保ってfalse。
	static bool Build( const FDrawQuad3DData& Data, const FCamera& Camera, TArray<FDrawVertex3D>& Out );
	// 左上・右上・右下・左下のカメラ基準のずれから画像を作る。
	// つぶれた三角形は省略。不正な座標ではOutを保ってfalse。
	static bool BuildCorners( const FVector3& Position, const float32 ( &Corners )[8], const FCamera& Camera, TArray<FDrawVertex3D>& Out );
};
