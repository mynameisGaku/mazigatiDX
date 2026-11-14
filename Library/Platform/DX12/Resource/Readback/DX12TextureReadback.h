// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Math/Color.h"
#include <d3d12.h>
#include <wrl/client.h>

// RGBA8画像をCPUへ読む領域を所有する。GPU完了前に再作成・破棄しない。
class CDX12TextureReadback
{
public:
	// GPU領域の再利用と寿命を一つの所有者で管理する。
	CDX12TextureReadback() = default;
	CDX12TextureReadback( const CDX12TextureReadback& ) = delete;
	CDX12TextureReadback& operator=( const CDX12TextureReadback& ) = delete;
	// 同じ寸法なら領域を再利用する。非対応形式・256MiBを超す画素列・作成失敗ではfalse。
	bool InitializeAfterGPUIdle( ID3D12Device* Device, ID3D12Resource* Texture );
	// 元の状態を復元するコピーを記録する。資源と命令列はGPU完了まで所有側が保持する。
	bool RecordCopy( ID3D12GraphicsCommandList* Commands, ID3D12Resource* Texture, D3D12_RESOURCE_STATES State );
	// GPU完了後に行の余白を除いて読む。失敗時はOutを保つ。通常撮影では不透明にする。
	bool ReadAfterGPUIdle( TArray<FColor32>& Out, bool bOpaque ) const;
	// 所有するGPUの完了または喪失後に領域を戻す。
	void ResetAfterGPUIdle();
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> Buffer; // GPUのコピー先とCPUの読み取り元。
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint = {}; // コピー時の行の余白と先頭位置。
	uint64 BufferBytes = 0; // Mapへ渡す読み取り範囲。
	int32 Width = 0, Height = 0; // 余白を除いた画素列の寸法。
};
