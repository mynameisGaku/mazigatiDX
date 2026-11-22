// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryVertex.h"
#include "Library/Core/Render/GeometryBufferDraw.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"
#include "Library/Core/Render/RenderTargetFormat.h"
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
	// いま描いている先（1 番目の色の描画先）の画素形式。パイプラインを、描画先に合わせて作るために使う。
	virtual ERenderTargetColorFormat GetColorFormat() const { return ERenderTargetColorFormat::RGBA8; }
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
	// 登録済みカスタムパイプラインで、頂点・添字・定数を、バッファとして登録せずに渡して描く（1回限りの描画）。条件はDrawGraphicsPipelineと同じ。
	// 未対応・フレーム外・不正な入力はfalseで記録しない。
	virtual bool DrawGraphicsPipelineTransient( const FGraphicsPipelineTransientDraw& Draw ) { (void)Draw; return false; }
	// 結果を待たなくてよい呼び出し元のための、DrawGraphicsPipeline。既定は、DrawGraphicsPipeline と同じ（同期）。
	// 待たずに積める実行先では、true は「描いた」ではなく「受け付けた」。受け付けたあとに実行先が拒否した描画は、ログへ出して捨てる。
	// 拒否になりうる条件（初めて使う組み合わせ・保持できる量の上限の近く・書き込み中の画像を読む描画）は、同期で実行して、本当の結果を返す。
	// 引数の配列は、呼び出しの間だけ借りる（待たずに積むときは、呼び出しの中で複写する）。FGraphicsPipelineDraw::PinnedBytes が 0 のときは、同期で実行する。
	virtual bool QueueGraphicsPipeline( const FGraphicsPipelineDraw& Draw ) { return DrawGraphicsPipeline( Draw ); }
	// 結果を待たなくてよい呼び出し元のための、DrawGraphicsPipelineTransient。条件は QueueGraphicsPipeline と同じ。FGraphicsPipelineTransientDraw::VertexStride が 0 のときは、同期で実行する。
	virtual bool QueueGraphicsPipelineTransient( const FGraphicsPipelineTransientDraw& Draw ) { return DrawGraphicsPipelineTransient( Draw ); }
	// 既存の描画より後に深度を1へ消す。クリップに関係なく全体を対象にする。
	virtual void ClearDepth() = 0;
};
