// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"

#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Interfaces/Render/IRenderTextRasterizer.h"
#include "Library/Core/Render/RenderClipRect.h"

// APIとUIに依存せず、2Dの形状を共通の頂点へ展開する。文字画像の作成先とGPU画像は借用する。
class CGeometryCanvas2D : public IRenderCanvas2D
{
  public:
	// フレーム中の借用参照と一時画像の所有を複写しない。
	CGeometryCanvas2D() = default;
	CGeometryCanvas2D( const CGeometryCanvas2D & ) = delete;
	CGeometryCanvas2D &operator=( const CGeometryCanvas2D & ) = delete;
	// 画像所有者より先に破棄し、一時画像の番号を返す。
	~CGeometryCanvas2D();
	// 既に完了した描画先へ接続する。借用先はEndFrameまで生存させる。
	bool BeginFrame( IRenderGeometry3D &InGeometry, IRenderTextureResources &InTextures, IRenderTextRasterizer &InText, FRenderClipRect &InClip, float32 InScale = 1 );
	// 一時画像の番号を返し、借用先を外す。画像のGPU実体は所有先が完了まで保持する。
	void EndFrame();
	// 現フレームの頂点・文字画像の登録がすべて成功した場合にtrue。
	bool DidFrameSucceed() const;
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
	// 独立した文字画像を作り、現在のクリップと倍率で描く。
	void Text( float32 X, float32 Y, FColor32 Color, const char *Begin, const char *End ) override;
	// 借用画像を凸四角形に貼る。UVは左上と右下。
	void Image( const FTextureDrawView &Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color ) override;
	// 現在の範囲との共通部分を積む。空なら描かない。
	void PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 ) override;
	// 直前のクリップ範囲へ戻す。Pushと対にする。
	void PopClip() override;
	// 後続の画像の補間を設定する。フレーム外はfalse。
	bool SetTextureFilter( bool bLinear ) override;
	// 図形の端の補間を指定する。
	void SetAntiAlias( bool bEnabled ) override;

  private:
	// 2D座標から共通の同次座標を作る。倍率は描画先の画素へだけ適用する。
	FGeometryVertex Vertex( float32 X, float32 Y, FColor32 Color, float32 U = 0, float32 V = 0 ) const;
	// 凸な点列の塗りつぶし、輪郭、端の補間を三角形へ展開する。
	void Polygon( const float32 *Points, int32 Count, FColor32 Color, bool bFill, float32 Thickness );
	// 2D専用の深度なし・通常合成の状態で頂点を渡す。
	void Draw( const TArray<FGeometryVertex> &Vertices, const FTextureDrawView *Texture = nullptr );
	IRenderGeometry3D *Geometry = nullptr; // ゲーム描画の頂点の実行先。
	IRenderTextureResources *Textures = nullptr; // 一時的な文字画像を登録する既存の所有先。
	IRenderTextRasterizer *TextRasterizer = nullptr; // CPUの文字画像だけを作る借用先。
	FRenderClipRect *Clip = nullptr; // 基本3Dと共有する描画先画素の範囲。
	TArray<FRenderClipRect> ClipStack; // 復元する範囲。描画命令列ごとに呼出側が対を保つ。
	TArray<FTextureResourceHandle> TextImages; // このフレームの文字画像。GPU完了まで実体は所有先に残る。
	uint64 TextBytes = 0; // 一フレームに登録する文字画像は合計32MiBまで。
	int32 Width = 0, Height = 0; // 固定の描画先画素数。
	float32 Scale = 1; // 撮影用の論理座標から描画先への倍率。
	bool bFailed = false; // 呼出側が保存や表示の失敗を確認するための累積結果。
	bool bAntiAlias = true; // 図形の端に被覆率を付けるか。
	bool bLinear = false; // 後続の画像へ適用する補間。
};
