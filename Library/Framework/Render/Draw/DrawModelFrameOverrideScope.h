// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideData.h"
#include "Library/Math/Matrix4x4.h"

class IAssetBackend;

// 描画1回だけframe poseを上書きし、描画前のbackend状態へ戻す。呼出側はbackendとmodel ownerをscope終了まで保持する。
class FModelFrameOverrideScope
{
public:
	// BackendでModelの全frame状態を保存し、全poseのquery/applyが成功したときだけ適用済みにする。失敗時は未適用とし、途中apply分は逆順に戻す。
	FModelFrameOverrideScope( IAssetBackend& Backend, int32 Model, const FDrawModelFrameOverrideData* Overrides, int32 NumOverrides );
	// 残る復元frameがあればscope終了時に逆順で最後の復元を試す。
	~FModelFrameOverrideScope();
	FModelFrameOverrideScope( const FModelFrameOverrideScope& ) = delete;
	FModelFrameOverrideScope& operator=( const FModelFrameOverrideScope& ) = delete;
	FModelFrameOverrideScope( FModelFrameOverrideScope&& ) = delete;
	FModelFrameOverrideScope& operator=( FModelFrameOverrideScope&& ) = delete;

	// 全poseの一時設定に成功し、復元をまだ試していないかを返す。
	bool IsApplied() const;
	// 元のpose状態へ逆順に戻す。失敗したframeだけを次回呼出しで再試行する。
	bool Restore();

private:
	struct FFrameState
	{
		int32 Frame = -1; // 復元対象のbone番号。
		bool bOverridden = false; // 適用前にbackendが追跡していた上書き状態。
		FMatrix4x4 Matrix; // 適用前に追跡していたpose値。
		bool bPendingRestore = false; // 一時poseが残り、復元を試す必要がある状態。
	};

	// 復元が必要なframeだけを、最後に適用したものから戻す。
	bool RestorePending();
	// まだ復元を試す必要があるframeが残っているか調べる。
	bool HasPendingRestore() const;

	IAssetBackend* Backend = nullptr; // 復元先backendの借用参照。
	int32 Model = -1; // 一時poseを適用するmodel handle。
	TArray<FDrawModelFrameOverrideData> RequestedOverrides; // 呼出側から値snapshotした一時pose群。
	TArray<FFrameState> PreviousStates; // apply前に全件取得した復元値。
	bool bApplied = false; // 全pose設定に成功し、まだRestoreを試していない状態。
	bool bRestoreStarted = false; // 最初の復元処理をすでに試した状態。
};
