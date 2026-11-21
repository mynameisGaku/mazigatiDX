// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineBinding.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferBinding.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureBinding.h"

// 1 フレームの記録が参照する GPU 資源（バッファ・画像・カスタムパイプライン）を、提出の完了まで、重複なく・上限内で保持する。
// 記録のあとに登録解除・更新されても、保持した参照が、GPU の使い終わるまで実体を生かす。保持の上限を超える指定は、何も変えずに拒否する。
class CDX12FramePins
{
  public:
	// 保持できる上限（バッファの数・合計の大きさ・画像の数・パイプラインの数）。
	static constexpr int32 MaxBuffers = 256;
	static constexpr uint64 MaxBufferBytes = 256ull * 1024 * 1024;
	static constexpr int32 MaxTextures = 256;
	static constexpr int32 MaxPipelines = 256;

	// 保持しているものを全部手放す（新しいフレームの開始）。
	void Reset();
	// 状態を変えずに、バッファ群を加えても上限内か調べる。重複した資源は、数にも大きさにも数えない。新規の数と、増える大きさを返す。
	bool CanCommitBuffers( TArrayView<const FDX12BufferBinding> Candidates, int32 &OutNewCount, uint64 &OutAddedBytes ) const;
	// バッファ群を、上限内のときだけ、重複なく保持する。
	bool CommitBuffers( TArrayView<const FDX12BufferBinding> Candidates );
	// 画像 1 枚を保持する（保持済みなら何もしない）。資源がない・上限を超える場合は false。
	bool PinTexture( const FDX12TextureBinding &Texture );
	// カスタムパイプラインの描画が使う画像群とパイプラインを、上限内のときだけ、重複なく保持する。
	bool CommitGraphicsAssets( TArrayView<const FDX12TextureBinding> Textures, const FDX12GraphicsPipelineBinding &Pipeline );
	// 保持した参照を、提出の記録へ移して（出力へ追加して）、手放す。
	void MoveTo( TArray<FDX12BufferBinding> &OutBuffers );
	void MoveTo( TArray<FDX12TextureBinding> &OutTextures, TArray<FDX12GraphicsPipelineBinding> &OutPipelines );

  private:
	TArray<FDX12BufferBinding> Buffers; // 重複しない GPU バッファ参照。
	TArray<FDX12TextureBinding> Textures; // 重複しない画像参照。
	TArray<FDX12GraphicsPipelineBinding> Pipelines; // 重複しないカスタム PSO。
	uint64 BufferBytes = 0; // 保持するバッファ全体の合計（MaxBufferBytes まで）。
};
