// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Deferred/DeferredDemoState.h"

class FDrawContext;

//=============================================================================
// CDeferredView
//   夜の広場（動く光 24 個・光る溝・球と柱）を、2 つの経路で描いて、画面へ見せる。
//   - Forward: 物を、光の計算つきで、場面の色（16 ビットの浮動小数点の描画先）へ直接描く。
//   - Deferred: ジオメトリのパスで、物の面の情報（地の色・法線・深度・自分で光る色）を G-buffer へ書き、
//     ライティングのパスで、G-buffer の画素ごとに、深度から世界の位置を求めて、光を当てる。光は、物の数ではなく、画素の数だけ計算する。
//   どちらも、同じ光の式（ShadeView）を使うので、同じ絵になる（違いが出たら、G-buffer の符号化か、位置の再構成の誤りが分かる）。
//   半透明の物（ガラスの球と板）は、不透明の物のあとに、奥から手前の順に、アルファ合成で重ねる（Forward 透明合成）。
//   Forward 経路は、ハードウェアの奥行きバッファで、不透明の物より奥の画素を省く。Deferred 経路は、場面の色の描き先が別の奥行きバッファを持つので、G-buffer の深度をシェーダーで読んで省く。
//   最後に、場面の色へ露出とトーンマップ（ACES）を掛けて、画面へ貼る。
//=============================================================================
class CDeferredView : public CGameObject
{
public:
	explicit CDeferredView( const TSharedPtr<FDeferredDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 場面の物をすべて描く（bGeometryPass が true なら G-buffer へ書くピクセルシェーダー、false なら Forward のピクセルシェーダー）
	void RecordScene( FDrawContext& CommandList, bool bGeometryPass ) const;

	// 半透明の物を、奥から手前の順に、アルファ合成で、いまの描画先（場面の色）へ重ねる。bDeferred が true なら、G-buffer の深度で奥の画素を省く（G-buffer を画像の段に置く）
	void RecordTransparent( FDrawContext& CommandList, bool bDeferred ) const;

	// ライティングのパス: G-buffer を読んで、場面の色の全体へ、光を当てた色を描く
	void RecordLighting( FDrawContext& CommandList ) const;

	// 場面の色を、露出とトーンマップを掛けて、画面へ貼る
	void RecordComposite( FDrawContext& CommandList ) const;

	TSharedPtr<FDeferredDemoState> State;
};
