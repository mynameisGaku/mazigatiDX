// SPDX-License-Identifier: MIT
#pragma once
#include "DX12SyncSubmitter.h"
#include "DX12TextureRecord.h"
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"

// 画像の GPU 資源（読み取り画像・compute 画像・描画先・深度）を作って、初期の内容を同期で転送・消去し、SRV を用意する。
// 登録・数の上限・能力の検査は、呼び出し側（CDX12TextureStore）が済ませる。作った実体は FDX12TextureRecord で返す（失敗時は Out を変えない）。
class CDX12TextureCreator
{
  public:
	// 借りる先は所有しない。所有者より長く生きるものを渡し、GPU完了後に Disconnect する。
	void Connect( ID3D12Device *InDevice, CDX12SyncSubmitter *InSubmitter, CDX12DescriptorPool *InPool );
	// 借りた先を手放す。
	void Disconnect();
	// CPU の画素から RGBA8 画像を作る。bWritable なら UAV 書き込みも許可する。bMips なら、半分ずつに縮めた段も作って全部転送する（作れない大きさ・書き込み可の画像は段なし）。
	bool CreateRGBA8( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, bool bMips, FDX12TextureRecord &Out );
	// CPU の画素から、UAV 読み書きできる RGBA16F 画像を作る（画素は有限値であること）。
	bool CreateRGBA16F( int32 Width, int32 Height, const FColor *Pixels, FDX12TextureRecord &Out );
	// 指定の色で消去した、色の描画先（RGBA8・RGBA16F・R32Float）を作る。作成時の消去値の記録（InitialClearColor 系）は、呼び出し側が行う。
	bool CreateColorRenderTarget( int32 Width, int32 Height, DXGI_FORMAT Format, const float32 ClearValues[4], EDX12TextureKind Kind, FDX12TextureRecord &Out );
	// 指定の深度で消去した、読み取れる D32 深度を作る（DSV と、R32_FLOAT の SRV を持つ）。
	bool CreateDepthD32( int32 Width, int32 Height, float32 ClearDepth, FDX12TextureRecord &Out );

  private:
	// 画像資源を作り、Fill が書いた内容を、転送領域経由で全段へ転送して、読み取りの状態へ移す（完了まで待つ）。
	template <typename TFill>
	bool UploadTexture( const D3D12_RESOURCE_DESC &Desc, UINT NumLevels, TFill &&Fill, FDX12TextureRecord &Created );
	// 記述子を借り、画像の SRV を作る。
	bool AttachShaderResourceView( FDX12TextureRecord &Created, DXGI_FORMAT Format, UINT MipLevels );
	ID3D12Device *Device = nullptr; // 借りた GPU。
	CDX12SyncSubmitter *Submitter = nullptr; // 借りた同期提出器。
	CDX12DescriptorPool *Pool = nullptr; // 借りた記述子の貸出先。
};
