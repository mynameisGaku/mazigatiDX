// SPDX-License-Identifier: MIT
#pragma once
#include "DX12SyncSubmitter.h"
#include "DX12TextureRecord.h"
#include "Library/Core/Collection/Array.h"

// 登録した画像の全画素を、CPU へ同期で読み戻す。形式ごとの変換は、呼び出し側（CDX12TextureStore）が行う。
class CDX12TextureReader
{
  public:
	// 借りる先は所有しない。所有者より長く生きるものを渡し、GPU完了後に Disconnect する。
	void Connect( ID3D12Device *InDevice, CDX12SyncSubmitter *InSubmitter );
	// 借りた先を手放す。
	void Disconnect();
	// 画像の全画素を、行ごとに詰めた生のバイト列へ読み戻す（形式と寸法が一致しなければ false）。失敗時は出力を変えない。
	// 画像は、定常の読み取りの状態であること。読み戻しのあとも、同じ状態へ戻す。
	bool ReadRows( const FDX12TextureRecord &Record, DXGI_FORMAT ExpectedFormat, UINT BytesPerPixel, TArray<uint8> &OutRows, UINT &OutRowBytes );

  private:
	ID3D12Device *Device = nullptr; // 借りた GPU。
	CDX12SyncSubmitter *Submitter = nullptr; // 借りた同期提出器。
};
