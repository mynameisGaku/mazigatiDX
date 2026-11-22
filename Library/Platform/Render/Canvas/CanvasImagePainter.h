// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"

// 画像・スプライト・タイル・円ゲージを、2Dのキャンバスへ描く。変換・補間・色は、実行の状態から取る。
class CCanvasImagePainter
{
public:
	explicit CCanvasImagePainter( FCanvasExecution& InRun );
	// 画像の命令なら実行して true。ほかの命令なら false。
	bool Execute( const FDrawCommandView& View );
	// 画像を、4点（左上・右上・右下・左下）へ、変換してから貼る。
	void DrawImage( const FTextureDrawView& Image, const float32 ( &Points )[8], const float32 ( &UV )[4] );

private:
	// 画像の中の矩形を、UV にする。画像の外へ出る矩形は false。
	bool SourceUV( const FTextureDrawView& Image, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 ( &UV )[4] );
	// 反転の指定を、UV の入れ替えにする（位置はそのまま）。
	void FlipUV( const FDrawCommandView& View, float32 ( &UV )[4] );
	// 画像を、凸な多角形（x, y, u, v の頂点列）へ、変換してから貼る。
	void DrawImagePolygon( const FTextureDrawView& Image, TArray<float32>& Vertices );
	// 画像の中の矩形を、基準点まわりに拡大・回転して描く（Sprite / SpriteRect / SpritePivot の共通）。
	// 反転は、基準点を軸にする（反転しても、基準点は (X, Y) のまま）。
	void DrawSprite( const FDrawCommandView& View, const FTextureDrawView& Image, float32 X, float32 Y, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 Angle, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH );
	void Unsupported( const FDrawCommandView& View, const char* Reason );
	FColor32 Color( FDrawRgb Value );
	bool GetImage( int32 Handle, FTextureDrawView& Image );
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	IRenderCanvas2D*& Canvas; // Run.Canvas の別名。
	bool& bStopped; // Run.bStopped の別名。
	bool& bLinearFilter; // Run.State.bLinearFilter の別名。
	bool& bExplicitFilter; // Run.State.bExplicitFilter の別名。
	int32 ( &AddColorValue )[3]; // Run.State.AddColorValue の別名。
	FCanvasTransform& Transform; // Run.State.Transform の別名。
};
