// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"

class IDrawExecutor;
class CSceneDrawList;
struct FSceneDrawSnapshotData;

// カメラと項目ごとの命令・所有参照を共有する不変の描画一覧。コピー同士は別スレッドで読める。
class FSceneDrawSnapshot
{
public:
	// 既定値は未作成。空の一覧を確定した値とは区別する。
	FSceneDrawSnapshot() = default;
	// 正常に確定された一覧かを返す。
	bool IsValid() const;
	// 確定した描画項目数を返す。未作成なら0。
	int32 GetNumItems() const;
	// カメラ命令を含めて所有するword数を返す。未作成なら0。
	int32 GetNumWords() const;
	// 呼出し側が実行先のスレッド・フレームを保証する。項目ごとに独立したSpanとして再生する。
	// 未作成ならfalse。資源の内容は記録後から使用完了まで変更せず、GPUの寿命は既存実行先が保つ。
	bool Replay( IDrawExecutor& Executor ) const;
	// この値の参照を手放す。同じ値の照会・再生と並行して呼ばない。
	void Reset();

private:
	friend class CSceneDrawList;
	TSharedPtr<const FSceneDrawSnapshotData> Data; // 再生中に変更されない共有の命令と参照。
};
