// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryVertex.h"
#include "Library/Core/Render/GeometryBufferDraw.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"
#include "Library/Core/Render/TextureDrawView.h"

// APIに依存しない頂点色・画像の3D実行先。公開のフロント側はゲームスレッド・フレーム内で呼ぶ。
// バックエンド側の実行先は、その所有スレッドだけで操作し、両側から同じ実体を並行に触らない。
class IRenderGeometry3D
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderGeometry3D() = default;
	// 論理画面の幅。
	virtual int32 GetWidth() const = 0;
	// 論理画面の高さ。
	virtual int32 GetHeight() const = 0;
	// 頂点と状態を呼び出し中に複写し、前の命令より後に描く。
	// VerticesはNumVertices個の有限な同次座標。失敗時はfalseで描画を記録しない。
	virtual bool DrawGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State ) = 0;
	// 同じ頂点に借用画像を貼る。TextureはGPU完了まで所有側が生存させる。
	// 画像と頂点色の積のAlphaが0なら色・深度を更新しない。
	// 未対応の実行先はfalse。頂点のUVと画像のbindingは呼び出し中に複写する。
	virtual bool DrawTexturedGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView& Texture ) { return false; }
	// GPU上の頂点・indexを読み戻さず描く。Textureは省略可能。未対応・範囲不正・別所有先はfalseで記録しない。
	// 頂点の有限性、indexがVertexCount未満であること、GPUの実行時点の内容は呼出側が保証する。
	// 同じキューへ先に提出したcomputeを参照できる。記録後の番号解除でも、実体はGPU完了まで保持する。
	virtual bool DrawBufferGeometry( const FGeometryBufferDraw& Draw, const FGeometryDrawState& State, const FTextureDrawView* Texture = nullptr ) { return false; }
	// 登録済みカスタムパイプラインとGPUバッファ・画像で描く。複数描画先（MRT）パスにも対応する。
	// パスや状態と一致しないパイプライン、範囲外バッファ、書き込み中ターゲットとの競合時はfalseで記録しない。
	virtual bool DrawGraphicsPipeline( const FGraphicsPipelineDraw& Draw ) { (void)Draw; return false; }
	// 既存の描画より後に深度を1へ消す。クリップに関係なく全体を対象にする。
	virtual void ClearDepth() = 0;
};
