// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Object/Object.h"
#include "Library/Math/Matrix4x4.h"

class CModel;
class CModelInstance;
class IAssetBackend;

// 専用モデルinstanceとbackendを借り、任意時刻の骨の親基準行列を読む。Createで時刻0をprobeするが、backend寿命内で使う。
class CModelAnimationSampler : public CObject
{
public:
	// メインスレッドで対応backendと有効なmesh/clipから専用instanceを作り、時刻0をprobeする。非対応・未準備・不正入力は nullptr。
	static TSharedPtr<CModelAnimationSampler> Create( const TSharedPtr<CModel>& TargetModel, const TSharedPtr<CModel>& AnimationSource, int32 AnimIndex );

	// メインスレッドでframe番号とnative時間を検証し、有限な完全local poseを読めたときだけ Out を更新する。
	bool SampleFrameLocalMatrix( int32 Frame, float32 Time, FMatrix4x4& OutMatrix ) const;

	// Create前の安全な空状態を作る。空状態でのSampleはfalseを返す。
	CModelAnimationSampler() = default;

private:
	TSharedPtr<CModelInstance> Instance; // 専用duplicateと添付アニメーションの寿命を保つ。
	IAssetBackend* Backend = nullptr; // 作成時backend以外へ誤ったnative handleを渡さない。寿命はAssetServiceに従う。
	int32 AttachIndex = -1; // 専用instance上の独立した添付番号。
	int32 FrameCount = 0; // 作成時に確認した対象骨の件数。
	float32 ClipLength = 0.0f; // 作成時に確認したnative時間の終端。
};
