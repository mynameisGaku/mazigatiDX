// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Platform/DX12/Pipeline/Geometry/DX12GeometryPipelineStore.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Core/Render/RenderClipRect.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferBinding.h"
class CDX12BufferStore;
#include <d3d12.h>
#include <wrl/client.h>

// 基本3Dと2Dの頂点をGPUへ直接記録する。UIの型や記録順に依存しない。
class CDX12GeometryRenderer : public IRenderGeometry3D
{
  public:
	// 所有するGPU資源とフレーム状態は複写しない。
	CDX12GeometryRenderer() = default;
	CDX12GeometryRenderer( const CDX12GeometryRenderer & ) = delete;
	CDX12GeometryRenderer &operator=( const CDX12GeometryRenderer & ) = delete;
	// GPUと寸法を固定する。撮影時は指定した深度を共有し、通常時は専用の深度を作る。
	// BuffersとQueueは同時に指定し、同じGPU・キューを使用する。Buffersは本描画器より長く生存させる。不一致はfalse。
	bool Initialize( ID3D12Device *InDevice, int32 InWidth, int32 InHeight, ID3D12Resource *InDepth = nullptr, ID3D12DescriptorHeap *InDepthHeap = nullptr, CDX12BufferStore *InBuffers = nullptr, ID3D12CommandQueue *InQueue = nullptr );
	// GPU待ち後だけ呼ぶ。前フレームの頂点を解放して深度を消す。
	bool BeginFrame( ID3D12GraphicsCommandList *InCommands, D3D12_CPU_DESCRIPTOR_HANDLE InRtv, ID3D12DescriptorHeap *InSrvHeap = nullptr, const FRenderClipRect *InClip = nullptr );
	// 記録先を外す。GPU上で使用中の頂点は保持する。
	void EndFrame();
	// 論理幅。
	int32 GetWidth() const override;
	// 論理高さ。
	int32 GetHeight() const override;
	// 頂点を所有コピーし、その呼出位置でGPUへ描画を記録する。
	bool DrawGeometry( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State ) override;
	// 既存の画像記述子を借りてUV付きの頂点を描く。
	bool DrawTexturedGeometry( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State, const FTextureDrawView &Texture ) override;
	// GPU頂点・indexの範囲を検査して直接描く。実体は登録解除後もフレーム完了まで保持する。
	bool DrawBufferGeometry( const FGeometryBufferDraw &Draw, const FGeometryDrawState &State, const FTextureDrawView *Texture = nullptr ) override;
	// 現在の命令位置で描画先全体の深度を消す。
	void ClearDepth() override;
	// クリップと深度を変えず、全画素の色だけを置き換える。不正な状態・資源不足はfalse。
	bool ClearColor( FColor32 Color );

  private:
	// 画像の有無だけを分岐し、同じ検査とアップロードを使う。
	bool DrawInternal( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State, const FTextureDrawView *Texture );
	// 共通の描画状態・画像heap・記録中の条件を検査する。
	bool ValidateDrawState( const FGeometryDrawState &State, const FTextureDrawView *Texture ) const;
	// GPUへ即時に記録する描画。頂点資源はGPU完了まで保持する。
	struct FPendingDraw
	{
		ID3D12PipelineState *Pipeline = nullptr; // 所有者のキャッシュにある状態。
		D3D12_VERTEX_BUFFER_VIEW Vertices = {}; // 所有者が保持する頂点領域。
		D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // 頂点のつなぎ方。
		UINT64 TextureBinding = 0; // 借用画像のGPU記述子。0なら頂点色だけ。
		UINT SamplerIndex = 0; // 補間と外側の参照設定。
		D3D12_INDEX_BUFFER_VIEW Indices = {}; // 省略可能なGPU上の頂点番号列。
		UINT NumIndices = 0; // 0なら番号を使わない。
		UINT NumVertices = 0; // 描く数。
	};
	// 検査と所有コピーを終えた描画を、現在のクリップでGPUへ記録する。
	void DrawPrepared( const FPendingDraw &Pending );
	// 頂点バッファの所有参照。コンテナ自体はCOMの取得窓口を扱わない。
	struct FUploadRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPU完了まで保持する頂点。
	};
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUへの参照。
	Microsoft::WRL::ComPtr<ID3D12Resource> Depth; // D32の深度画像。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvHeap; // 深度画像の記述子。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SamplerHeap; // 補間2種類×参照方法4×4。
	UINT SamplerStride = 0; // sampler記述子の幅。
	ID3D12DescriptorHeap *SrvHeap = nullptr; // 今フレームの画像記述子を借用する。
	CDX12GeometryPipelineStore Pipelines; // シェーダー・配置・PSOの専用所有者。描画記録や深度を所有しない。
	CDX12BufferStore *Buffers = nullptr; // GPUバイト列の所有先。GPUと描画キューを共有する借用先。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> BufferQueue; // 別キューへ再初期化された所有先を混在させない。
	TArray<FDX12BufferBinding> BufferPins; // 記録後に登録解除されても残す、重複しないGPU参照。
	uint64 PinnedBytes = 0; // 描画が保持するバッファ全体の合計。256MiBまで。
	int32 BufferDrawCount = 0; // 一フレームのGPUバッファ描画は1024回まで。
	TArray<FUploadRecord> Uploads; // GPU完了まで保持する頂点。
	ID3D12GraphicsCommandList *Commands = nullptr; // 今フレームだけ借用する記録先。
	const FRenderClipRect *Clip = nullptr; // 呼出位置の切り取り範囲。フレーム中だけ借用する。
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv = {}; // 今フレームだけ借用する画面。
	uint64 UploadBytes = 0; // 今フレームに保持した頂点のバイト数。
	int32 Width = 0, Height = 0; // 固定の論理解像度。
};
