// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 資源へのコピーを、GPU完了を待たずに積むための、転送領域（UPLOADバッファ）と命令列の使い回し。
//   Open   : 大きさSize以上の転送領域（開いたままマップ済み）と、記録できる命令列を借りる。CPUが転送領域へ書き、命令列へコピーを記録する。
//   Submit : 命令列を閉じて、すぐ同じキューへ提出する（待たない）。同じキューなので、そのあとに積む描画より先に実行される。
//   Cancel : 提出せずに戻す。
//   CollectAfterGPUIdle : 同じキューのGPU完了後（フレームの境目）に、提出済みの転送領域と命令列を、再利用へ戻す。
// 1度に借りられるのは1つだけ（1つのスレッドから使う）。数・大きさに予算があり、超えたらOpenが失敗する（呼び出し側が、完了を待つ経路を使う）。
class CDX12CopyStaging
{
  public:
	// 借りた先と預かった資源は、Disconnect で手放す。
	CDX12CopyStaging() = default;
	CDX12CopyStaging( const CDX12CopyStaging & ) = delete;
	CDX12CopyStaging &operator=( const CDX12CopyStaging & ) = delete;
	// 借りた1回分。Submit か Cancel までの間だけ使う。
	struct FOpened
	{
		ID3D12Resource *Upload = nullptr; // コピー元（UPLOADバッファ）。
		uint8 *Mapped = nullptr; // CPUが書く先（Uploadの先頭）。
		ID3D12GraphicsCommandList *Commands = nullptr; // コピーを記録する、開いた命令列。
	};
	// 借りるデバイスとキューを決める。キューは所有しない。
	void Connect( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue );
	// 同じキューのGPU完了後に、預かった資源を全部手放し、借りた先も手放す。
	void Disconnect();
	// 転送領域（Size以上）と命令列を借りる。借りたままのものがある・予算超過・失敗は、Outを変えずにfalse。
	bool Open( uint64 Size, FOpened &Out );
	// 借りた命令列を閉じて、同じキューへ提出する（待たない）。閉じられなければ、借りたものを戻してfalse。
	bool Submit();
	// 提出せずに、借りたものを戻す。
	void Cancel();
	// 同じキューのGPU完了後に、提出済みの転送領域と命令列を、再利用へ戻す。
	void CollectAfterGPUIdle();

  private:
	// 書き込み用に開いたままマップしておく転送領域。
	struct FUploadBuffer
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // UPLOADヒープのバッファ。
		uint8 *Mapped = nullptr; // 開いたままのマップ先。
		uint64 Size = 0; // バッファの大きさ。
	};
	// コピーを記録する領域と命令列の組。
	struct FRecordingSet
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // GPU完了後はResetできる領域。
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 提出済みなら閉じた命令列。
	};
	// 大きさSize以上の転送領域を、再利用の置き場から取る（なければ作る）。
	bool AcquireUpload( uint64 Size, FUploadBuffer &Out );
	// 記録できる状態の領域と命令列を、再利用の置き場から取る（なければ作る）。
	bool AcquireRecording( FRecordingSet &Out );
	// 借りている転送領域を、再利用の置き場へ戻す。
	void ReturnUpload( FUploadBuffer &Upload );
	ID3D12Device *Device = nullptr; // 借りたGPU。
	ID3D12CommandQueue *Queue = nullptr; // 借りたDIRECTキュー。
	TArray<FUploadBuffer> FreeUploads; // GPU完了後に再利用できる転送領域。
	TArray<FRecordingSet> FreeRecordings; // GPU完了後に再利用できる命令列。
	TArray<FUploadBuffer> UsedUploads; // 提出済みで、GPU完了を待っている転送領域。
	TArray<FRecordingSet> UsedRecordings; // 提出済みで、GPU完了を待っている命令列。
	FUploadBuffer OpenUpload; // 今、借りている転送領域。
	FRecordingSet OpenRecording; // 今、借りている命令列。
	bool bOpen = false; // 借りているか。
	int64 FreeUploadBytes = 0; // 預かった転送領域の大きさの合計。
	int64 UsedUploadBytes = 0; // GPU完了を待っている転送領域の大きさの合計。
	static constexpr int32 MaxInFlight = 512; // GPU完了を待つコピーの数の上限（超えたら、Openが失敗する）。
	static constexpr int64 MaxInFlightBytes = 128ll * 1024 * 1024; // GPU完了を待つ転送領域の合計の上限。
	static constexpr int32 MaxFreeUploads = 128; // 預かる転送領域の数の上限。
	static constexpr int64 MaxFreeUploadBytes = 64ll * 1024 * 1024; // 預かる転送領域の合計の上限。
	static constexpr int32 MaxFreeRecordings = 128; // 預かる命令列の数の上限。
};
