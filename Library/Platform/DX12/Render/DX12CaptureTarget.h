// SPDX-License-Identifier: MIT
#pragma once
#include "DX12QueueCompletion.h"
#include "DX12TextureReadback.h"

// 画面外のRGBA8色・深度・命令列・読み戻しを所有する。ImGuiとウィンドウを必要としない。
class FDX12CaptureTarget
{
public:
	// GPU資源の所有先を複写しない。
	FDX12CaptureTarget() = default;
	FDX12CaptureTarget( const FDX12CaptureTarget& ) = delete;
	FDX12CaptureTarget& operator=( const FDX12CaptureTarget& ) = delete;
	// GPU完了または喪失を確認するまで資源を保持する。
	~FDX12CaptureTarget();
	// 同じGPUのDIRECTキューへ接続する。UIやOSウィンドウの初期化は行わない。
	bool Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue );
	// GPU完了後に描画先を用意して色と深度を消す。不正な寸法・二重開始ではfalse。
	bool Begin( int32 Width, int32 Height, FColor32 ClearColor );
	// 色をコピーして提出し、完了後に詰めたRGBA8を返す。失敗時はOutを保って資源を保持する。
	bool SubmitAndRead( TArray<FColor32>& Out, uint32 TimeoutMs = 10000 );
	// 記録中の色全体を消す。描画状態を変更するため、呼出側は後続描画の状態を設定する。
	bool ClearColor( FColor32 Color );
	// 最大60000msの範囲で終了を試す。falseなら資源を保持して再試行できる。
	bool TryShutdown( uint32 TimeoutMs = 10000 );
	// GPUへ描画を記録できる間だけtrue。
	bool IsRecording() const;
	// API固有の描画実装へ、記録中だけ命令列を貸す。
	ID3D12GraphicsCommandList* GetCommands() const;
	// 所有GPUをAPI固有の描画実装へ貸す。
	ID3D12Device* GetDevice() const;
	// 色の描画先を返す。初期化・Begin成功後にだけ使う。
	D3D12_CPU_DESCRIPTOR_HANDLE GetColorView() const;
	// 深度を使う描画実装へ資源を貸す。Begin成功後にだけ使う。
	ID3D12Resource* GetDepthTexture() const;
	// 深度の記述子を借用する。Begin成功後にだけ使う。
	ID3D12DescriptorHeap* GetDepthHeap() const;
	// 記録中の描画先の幅。
	int32 GetWidth() const;
	// 記録中の描画先の高さ。
	int32 GetHeight() const;
private:
	// GPU停止中に色・深度とコピー先を作る。最適化する初期消去色も一致させる。
	bool PrepareImages( int32 Width, int32 Height, FColor32 Clear );
	// 任意の色消去を行うGPUの状態を作る。UIの描画状態とは共有しない。
	bool PrepareClearPipeline();
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 資源の所有GPU。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画の提出先。
	Microsoft::WRL::ComPtr<ID3D12Resource> ColorTexture, DepthTexture; // 撮影中の色と深度。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ColorHeap, DepthHeap; // 色と深度の記述子。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // この描画先だけの記録領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // この描画先だけの命令列。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> ClearRoot; // 消去色をGPUへ渡す配置。
	Microsoft::WRL::ComPtr<ID3D12PipelineState> ClearPipeline; // 最適化色と異なる色でも警告なく塗るための状態。
	FDX12QueueCompletion Completion; // 先行提出の完了確認。
	FDX12TextureReadback Readback; // 撮影した色の読み戻し領域。
	FColor32 InitialClear; // 色画像の作成時に指定した消去色。
	int32 Width = 0, Height = 0; // 現在の色・深度の寸法。
	bool bRecording = false, bHealthy = true, bClosing = false; // 二重記録と、失敗・終了後の再利用を防ぐ。
};
