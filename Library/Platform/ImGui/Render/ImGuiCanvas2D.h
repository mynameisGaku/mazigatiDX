// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"

struct ImDrawList;
struct ImVec4;

// ImGuiの図形生成だけを再利用する。DX12・Vulkan・OpenGLのGPU型を持たない。
class FImGuiCanvas2D : public IRenderCanvas2D
{
public:
	// 論理画素の寸法を設定する。表示先の移動・拡大は各描画で主画面から取得する。不正な寸法はfalse。
	bool SetLogicalSize( int32 Width, int32 Height );
	// 撮影用の描画リストを借り、原点0・指定倍率へ記録する。nullptrで主画面へ戻す。
	void SetDrawList( ImDrawList* List, float32 Scale = 1 );
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
	// 既定フォントでUTF-8の範囲を描く。文字の切り取りを論理画素で行ってから主画面へ合わせる。
	void Text( float32 X, float32 Y, FColor32 Color, const char* Begin, const char* End ) override;
	// 借用画像を凸四角形に貼る。UVは左上と右下。
	void Image( const FTextureDrawView& Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color ) override;
	// 現在の範囲との共通部分を積む。空なら描かない。
	void PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 ) override;
	// 直前のクリップ範囲へ戻す。Pushと対にする。
	void PopClip() override;
	// 公式GPU実装の補間切り替えを使う。窓口がなければfalse。
	bool SetTextureFilter( bool bLinear ) override;
	// 図形の端の補間を指定する。
	void SetAntiAlias( bool bEnabled ) override;
private:
	// 明示した撮影先か、主画面の背景描画を返す。
	ImDrawList* GetDrawList() const;
	// 表示原点XYと論理座標からの倍率XYを返す。
	ImVec4 GetTransform() const;
	// 今回追加した頂点だけを、主画面の位置と実際の表示寸法へ変換する。
	void TransformVertices( ImDrawList& List, int32 FirstVertex ) const;
	int32 LogicalWidth = 0, LogicalHeight = 0; // 0なら表示先と同じ画素寸法として扱う。
	ImDrawList* DrawList = nullptr; // 撮影中だけ借りる記録先。
	float32 DrawScale = 1; // 撮影先へ写す2Dの倍率。
};
