// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/BufferResourceHandle.h"

// 初期データを持つGPUバッファの所有と転送。ゲームスレッドから呼び、ファイルやUIは扱わない。
// 初期経路は同期の作成と読み戻しだけ。描画・computeへの貸し出しや更新は別の契約で追加する。
class IRenderBufferResources
{
public:
	// 所有先は、提出済みのGPU処理の完了後に破棄する。
	virtual ~IRenderBufferResources() = default;
	// Bytesを呼び出し中に複写し、GPU転送の完了後に登録する。空・不正な入力・容量超過・GPU失敗は無効値。
	virtual FBufferResourceHandle CreateBuffer( TArrayView<const uint8> Bytes ) = 0;
	// 番号を直ちに無効にする。使用中の実体はGPU完了まで保持する。無効・他の所有先・二重解放は無視する。
	virtual void ReleaseBuffer( FBufferResourceHandle Buffer ) = 0;
	// 有効な資源のバイト数を返す。失敗時はOutSizeを保つ。
	virtual bool GetBufferSize( FBufferResourceHandle Buffer, uint64& OutSize ) const = 0;
	// 範囲をGPUからCPUへコピーし、完了後にOutを置き換える。範囲外・負数・GPU失敗時はOutを保つ。
	// 有効な資源の末尾を含む空範囲は成功し、Outを空にする。毎フレームの通常描画には使用しない。
	virtual bool ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8>& Out ) = 0;
};
