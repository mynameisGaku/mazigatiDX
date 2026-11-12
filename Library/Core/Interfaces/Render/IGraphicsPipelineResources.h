// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GraphicsPipelineDesc.h"
#include "Library/Core/Render/GraphicsPipelineHandle.h"

// カスタムグラフィックスパイプラインをGPU上へ作成・所有・遅延破棄する窓口。
// ゲームスレッドから呼び、描画の記録・提出・GPU待機とは責務を分ける。
class IGraphicsPipelineResources
{
  public:
	// 窓口経由の破棄を許可する。所有者がGPU完了までPSOとRootSignatureを生存させる。
	virtual ~IGraphicsPipelineResources() = default;
	// DXBCシェーダー・頂点入力・スロット・描画先数・状態を照合して登録する。失敗時は無効値。
	virtual FGraphicsPipelineHandle CreateGraphicsPipeline( const FGraphicsPipelineDesc &Desc ) = 0;
	// 番号を直ちに無効にし、記録・提出済みGPU処理の完了後に実体を解放する。
	virtual void ReleaseGraphicsPipeline( FGraphicsPipelineHandle Pipeline ) = 0;
};
