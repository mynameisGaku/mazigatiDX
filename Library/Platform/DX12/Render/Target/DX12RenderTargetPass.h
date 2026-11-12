// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Platform/DX12/Device/DX12QueueCompletion.h"
#include "Library/Platform/DX12/Render/Game/DX12GameRenderer.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureStore.h"
class CDX12TimestampStore;

// オフスクリーン色描画先と読み取れる深度への2D/3D描画を記録し、同じDIRECTキューへ提出する。
class CDX12RenderTargetPass : public IRenderSurface
{
  public:
	// GPUと提出中の参照を複写しない。
	CDX12RenderTargetPass() = default;
	CDX12RenderTargetPass( const CDX12RenderTargetPass & ) = delete;
	CDX12RenderTargetPass &operator=( const CDX12RenderTargetPass & ) = delete;
	// 提出済みのGPU処理が完了するかデバイスが喪失するまで待って破棄する。
	~CDX12RenderTargetPass() override;
	// 同じGPUとDIRECTキューを共有する画像・バッファ所有先へ接続する。UIや窓を必要としない。
	bool Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12TextureStore &InTextures, IRenderTextRasterizer &InText, ID3D12DescriptorHeap *InImageHeap = nullptr, CDX12BufferStore *InBuffers = nullptr, CDX12GraphicsPipelineStore *InGraphicsPipelines = nullptr, CDX12TimestampStore *InTimestamps = nullptr );
	// 同じキューのGPU完了後に、完了済みの提出参照を回収する。
	void CollectAfterGPUIdle();
	// GPU完了または喪失後に全資源を解放する。
	void ResetAfterGPUIdle();
	// 最大60000msの範囲で完了を待って終了を試す。
	bool TryShutdown( uint32 TimeoutMs = 10000 );
	// 描画先パスを開始する。不正な番号・寸法不一致・二重開始はfalse。
	bool Begin( const FRenderTargetPassDesc &Desc );
	// 描画先パスの命令を閉じて同じDIRECTキューへ提出する。自己参照競合・記録失敗時は提出せずfalse。
	bool End();
	// 描画先パスの記録中だけtrue。
	bool IsActive() const;
	// 記録中の2Dキャンバスを貸す。
	IRenderCanvas2D *GetCanvas2D() override;
	// 記録中の3D描画器を貸す。
	IRenderGeometry3D *GetGeometry3D() override;
	// 記録位置で全画素の色だけを置き換える。
	void RecordClear( FColor32 Color ) override;
	// 接続したDIRECTキューのGPU完了を待つ。
	bool WaitForGPU() override;
	// IRenderSurface経由で描画先パスを開始する。
	bool BeginRenderTargetPass( const FRenderTargetPassDesc &Desc ) override;
	// IRenderSurface経由で描画先パスを終了・提出する。
	bool EndRenderTargetPass() override;
	// パス内のコマンド列へGPUタイムスタンプの開始点を積む。
	bool BeginGpuTimestamp( FGpuTimestampHandle Query ) override;
	// パス内で開始したGPUタイムスタンプを閉じ、同じパス内で解決を記録する。
	bool EndGpuTimestamp( FGpuTimestampHandle Query ) override;

  private:
	// 1回のオフスクリーン描画パス提出が完了するまで保持するGPU参照。
	struct FSubmissionRecord
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // GPU完了まで再利用しない命令領域。
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 提出済みの描画命令列。
		TArray<FDX12TextureBinding> Colors; // 登録解除後も完了まで保持する1〜8枚の色描画先。
		FDX12TextureBinding Depth; // 登録解除後も完了まで保持する深度描画先。
		TArray<FDX12BufferBinding> Buffers; // パス内で描画したGPU頂点・indexバッファ参照。
		TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> Uploads; // パス内で作成した一時頂点アップロード領域。
		TArray<FDX12TextureBinding> Textures; // カスタム描画が参照した画像・読み取り深度。
		TArray<FDX12GraphicsPipelineBinding> Pipelines; // カスタム描画で解放済みでも完了まで保つPSO。
		uint64 Value = 0; // フェンス完了番号。
	};
	// 完了済みの提出レコードを回収する。
	bool CollectCompleted();
	// 深度未指定時の内部D32深度を用意する。同じ寸法なら現在内容と最適化消去値を保つ。
	bool PrepareFallbackDepth( int32 Width, int32 Height, float32 ClearDepth );
	// 最適化深度と異なる値でも警告を出さずに深度だけを初期化するパイプラインを用意する。
	bool PrepareDepthClearPipeline();
	// 現在の描画先の深度だけを指定値で塗りつぶす。
	bool ClearDepthWithShader( int32 Width, int32 Height, D3D12_CPU_DESCRIPTOR_HANDLE Dsv, float32 ClearDepth );
	// 作成時と異なる消去色でも警告を出さずに単一RTVを塗りつぶすパイプラインを用意する。
	bool PrepareColorClearPipeline();
	// 任意の色で1枚の色RTVを塗りつぶす（MRT時の個別クリア／RecordClear用）。
	bool ClearColorWithShader( int32 Width, int32 Height, D3D12_CPU_DESCRIPTOR_HANDLE Rtv, FColor32 Color );
	// 作成時消去色と一致すればClearRenderTargetViewを、不一致なら警告なしの全画面クリアを選ぶ。
	bool ClearColorAttachment( const FDX12TextureBinding &Target, FColor32 Color );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUへの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 通常描画・computeと共有するDIRECTキュー。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // パス提出の完了追跡フェンス。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CurrentAllocator; // 現在記録中のパス領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CurrentCommands; // 現在記録中のパス命令列。
	Microsoft::WRL::ComPtr<ID3D12Resource> FallbackDepth; // 深度指定省略時に使う内部D32深度。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> FallbackDepthHeap; // 内部深度用のCPU DSVヒープ。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> DepthClearRoot; // 任意深度消去用のルート署名。
	Microsoft::WRL::ComPtr<ID3D12PipelineState> DepthClearPipeline; // 任意深度消去用のPSO。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> ColorClearRoot; // 任意色消去用のルート署名。
	Microsoft::WRL::ComPtr<ID3D12PipelineState> ColorClearPipeline; // 任意色消去用のPSO。
	CDX12QueueCompletion Completion; // 終了・明示待機用の完了確認。
	CDX12TextureStore *Textures = nullptr; // 色描画先と深度を所有するストア。
	IRenderTextRasterizer *Text = nullptr; // 2D文字描画用のラスタライザー。
	ID3D12DescriptorHeap *ImageHeap = nullptr; // 描画用SRVヒープ。
	CDX12BufferStore *Buffers = nullptr; // 3D GPUバッファ描画の借用先。
	CDX12GraphicsPipelineStore *GraphicsPipelines = nullptr; // カスタムグラフィックスパイプラインの借用先。
	CDX12GameRenderer *Renderer = nullptr; // 2D/3D共通の描画記録器。
	TArray<FDX12TextureBinding> ActiveColors; // 現在記録中の1〜8枚の色描画先。
	FDX12TextureBinding ActiveDepth; // 現在記録中の深度描画先。
	TArray<FSubmissionRecord> Pending; // GPU未完了の提出レコード列。
	CDX12TimestampStore *Timestamps = nullptr; // 任意のGPUタイムスタンプ所有者。
	uint64 ActiveTimestampBatchId = 0; // パス中のタイムスタンプ計測に使うバッチ番号。
	uint64 NextFenceValue = 0; // 次に発行するフェンス番号。
	float32 FallbackClearDepth = 1.0f; // 内部深度の作成時クリア値。
	int32 FallbackWidth = 0, FallbackHeight = 0; // 内部深度の寸法。
	bool bFallbackInitialized = false; // 内部深度を初期化する命令がGPUへ提出済みか。
	bool bUsingExternalDepth = false; // 呼び出し側の深度資源を束縛しているか。
	bool bRecordClearFailed = false; // RecordClearの任意色クリアが失敗した印。
	bool bActive = false; // 描画先パスの記録中か。
	bool bHealthy = false; // GPU異常後の再利用を防ぐ印。
	bool bClosing = false; // 終了処理中の印。
	static constexpr int32 MaxPending = 16; // 同時保持する未完了パス提出の上限。
};
