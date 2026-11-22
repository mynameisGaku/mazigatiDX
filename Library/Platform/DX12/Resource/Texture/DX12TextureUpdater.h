// SPDX-License-Identifier: MIT
#pragma once
#include "DX12TextureCreator.h"
#include "DX12TextureRecord.h"
#include "Library/Platform/DX12/Resource/Upload/DX12CopyStaging.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 読み取り用RGBA8画像の全画素の更新を、GPU完了を待たずに積む（毎フレーム、CPUで作った画像を更新する使い方を軽くする）。
//   Update : 新しい版へ全画素を書く。使い終えた旧版の資源と記述子があれば再利用し、なければ新しく作る。
//            転送領域と命令列は CDX12CopyStaging で使い回し、すぐ同じキューへ提出する（待たない。同じキューなので、そのあとに積む描画より先に実行される）。
//   Adopt  : 更新で外れた旧版を受け取り、次のGPU完了後の再利用のために預かる。
//   CollectAfterGPUIdle : 同じキューのGPU完了後（フレームの境目）に、提出済みの転送領域と命令列を、再利用へ戻す。
// 記録済みの描画は、更新前の版の資源と記述子を参照し続ける（旧版は、GPU完了後にしか再利用しない）。ミップマップつき・compute画像は扱わない（呼び出し側が、完了を待つ経路を使う）。
class CDX12TextureUpdater
{
  public:
	// 借りた先と預かった資源は、Disconnect で手放す。
	CDX12TextureUpdater() = default;
	CDX12TextureUpdater( const CDX12TextureUpdater & ) = delete;
	CDX12TextureUpdater &operator=( const CDX12TextureUpdater & ) = delete;
	// 借りる先は所有しない。所有者より長く生きるものを渡し、GPU完了後に Disconnect する。
	void Connect( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12TextureCreator *InCreator, CDX12DescriptorPool *InPool );
	// 同じキューのGPU完了後に、預かった資源と記述子を全部手放し、借りた先も手放す。
	void Disconnect();
	// 段のない読み取り用RGBA8画像（Width x Height）の新しい版を作り、全画素を書いて提出する（完了は待たない）。条件外・予算超過・失敗は、Outを変えずにfalse（呼び出し側が、完了を待つ経路を使う）。
	bool Update( int32 Width, int32 Height, const FColor32 *Pixels, FDX12TextureRecord &Out );
	// 更新で外れた旧版を預かる。再利用できないもの（形式違い・段つき・大きすぎる）はfalse（呼び出し側が破棄する）。同じキューのGPU完了後だけ呼ぶ。
	bool Adopt( FDX12TextureRecord &Retired );
	// 同じキューのGPU完了後に、使い終えた転送領域と命令列を、再利用へ戻す。
	void CollectAfterGPUIdle();

  private:
	// 使わなかった版を戻す（再利用した版は預かりへ戻し、新しく作った版は破棄する）。
	void GiveBack( FDX12TextureRecord &Version, bool bRecycled );
	// 画像と記述子を破棄する。
	void Destroy( FDX12TextureRecord &Texture );
	ID3D12Device *Device = nullptr; // 借りたGPU。
	ID3D12CommandQueue *Queue = nullptr; // 借りたDIRECTキュー。
	CDX12TextureCreator *Creator = nullptr; // 借りた、画像を作る所有者。
	CDX12DescriptorPool *Pool = nullptr; // 借りた記述子の貸出先。
	TArray<FDX12TextureRecord> FreeVersions; // GPU完了後に再利用できる旧版（古い順）。
	CDX12CopyStaging Staging; // 転送領域と命令列の使い回し。提出まで行う。
	int64 FreeVersionBytes = 0; // 預かった旧版の大きさの合計。
	static constexpr int32 MaxFreeVersions = 128; // 預かる旧版の数の上限。
	static constexpr int64 MaxFreeVersionBytes = 64ll * 1024 * 1024; // 預かる旧版の合計の上限。
};
