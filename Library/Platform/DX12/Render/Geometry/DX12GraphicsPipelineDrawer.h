// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"
#include "Library/Core/Render/RenderTargetFormat.h"
#include "Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineBinding.h"
#include "Library/Platform/DX12/Render/Geometry/DX12FramePins.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureBinding.h"
#include "Library/Platform/System/WindowsInclude.h"
class CDX12BufferStore;
class CDX12TextureStore;
class CDX12GraphicsPipelineStore;
class CDX12UploadArena;
#include <d3d12.h>
#include <wrl/client.h>

// カスタムパイプラインの描画が、記録先の今の状態を読むための値。CDX12GeometryRenderer がフレームの間だけ持ち、描画のたびに貸す。
struct FDX12GraphicsDrawTarget
{
	ID3D12GraphicsCommandList *Commands = nullptr; // 記録先。
	ID3D12DescriptorHeap *SrvHeap = nullptr; // 画像の記述子の heap（なければ画像スロットは使えない）。
	uint32 NumColorViews = 0; // 束縛中の色描画先の数。
	const D3D12_CPU_DESCRIPTOR_HANDLE *ColorViews = nullptr; // 色描画先の記述子。
	const ERenderTargetColorFormat *ColorFormats = nullptr; // 色描画先ごとの画素形式。
	const uint64 *ForbiddenBindings = nullptr; // 書き込み中で、入力に束縛してはならない画像の記述子。
	uint32 NumForbiddenBindings = 0; // ForbiddenBindings の数。
	bool bHasDepth = false; // 深度を使えるか。
	D3D12_CPU_DESCRIPTOR_HANDLE DepthView = {}; // 深度の記述子。
	bool bHasArea = false; // 描く範囲（クリップ）が空でないか。空なら、何も描かずに成功として戻る。
	D3D12_VIEWPORT Viewport = {}; // 描く範囲の viewport。
	D3D12_RECT Scissor = {}; // 描く範囲の scissor。
};

// カスタムパイプラインの描画が、1 フレームの間に増やしていく数え。CDX12GeometryRenderer が持ち、描画のたびに貸す。
struct FDX12GraphicsDrawLedger
{
	CDX12FramePins *Pins = nullptr; // 記録が参照する資源の保持。
	int32 *NumGraphicsDraws = nullptr; // 今フレームのカスタム描画の数。
	uint64 *UploadBytes = nullptr; // 今フレームに転送領域へ載せた量。
	uint64 UploadTicket = 0; // 転送領域の今フレームの予約番号。
	bool *bHazardRejected = nullptr; // 書き込み先と入力の競合を検出した印。
};

// 登録済みカスタムパイプラインの描画を、検査して、記録先へ記録する。頂点・添字・定数を、登録したバッファから使う描画と、フレームの転送領域へ載せて使う描画の 2 種類。
// 記録先の状態は持たない（FDX12GraphicsDrawTarget と FDX12GraphicsDrawLedger で、描画のたびに受け取る）。
class CDX12GraphicsPipelineDrawer
{
  public:
	// 使う所有先を借りる（どれも、この描画器より長く生存させる）。Buffers と Queue は同じ GPU・キューを使うこと。
	void Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InBufferQueue, CDX12GraphicsPipelineStore *InGraphicsPipelines, CDX12BufferStore *InBuffers, CDX12TextureStore *InTextureStore, CDX12UploadArena *InUploadArena );
	// 登録したバッファを頂点・添字・スロットに使って描く。条件に合わなければ false で、何も記録しない。
	bool Record( const FGraphicsPipelineDraw &Draw, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger ) const;
	// 頂点・添字・定数を、フレームの転送領域へ載せて描く（バッファを登録しない）。
	bool RecordTransient( const FGraphicsPipelineTransientDraw &Draw, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger ) const;

  private:
	// パイプラインを取得し、束縛中の描画先（数・画素形式・深度）と合うか調べる。
	bool AcquirePipeline( FGraphicsPipelineHandle Handle, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsPipelineBinding &OutPipeline ) const;
	// 画像スロットに渡す画像を取得する。書き込み中の描画先を入力にする競合は、拒否して印を立てる。
	bool AcquireSlotTextures( TArrayView<const FTextureResourceHandle> Textures, const FDX12GraphicsPipelineBinding &Pipeline, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger, TArray<FDX12TextureBinding> &OutTextures ) const;
	// 定数を、256 の境界へ揃えて転送領域へ載せる（余りは 0）。
	bool UploadConstants( TArrayView<const FGraphicsConstantBytes> Constants, const FDX12GraphicsDrawLedger &Ledger, TArray<uint64> &OutGpuAddresses ) const;
	// 現在の描画先で書き込み禁止中の記述子か。
	static bool IsForbiddenBinding( const FDX12GraphicsDrawTarget &Target, UINT64 Binding );
	// 画像 heap に属する有効な記述子か。
	bool IsValidSrvHandle( const FDX12GraphicsDrawTarget &Target, UINT64 Binding ) const;

	ID3D12Device *Device = nullptr; // 記述子の幅を調べる GPU。
	ID3D12CommandQueue *BufferQueue = nullptr; // バッファの所有先と同じキューか確かめるための参照。
	CDX12GraphicsPipelineStore *GraphicsPipelines = nullptr; // パイプラインの所有先。
	CDX12BufferStore *Buffers = nullptr; // バッファの所有先（なければ、登録したバッファの描画はできない）。
	CDX12TextureStore *TextureStore = nullptr; // 画像の所有先。
	CDX12UploadArena *UploadArena = nullptr; // フレームの転送領域。
};
