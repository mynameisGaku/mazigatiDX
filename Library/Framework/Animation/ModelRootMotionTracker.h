// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Asset/Model/Model.h"
#include "Library/Math/Matrix4x4.h"

class CModelAnimationSampler;

//=============================================================================
// FModelRootMotionTracker
//   モデルのアニメーター（FModelAnimator）の、ルートモーションの係。選んだ骨が、再生でどれだけ動いたかを、
//   「まだ受け取っていない区間」として覚えておき、受け取るときに、その区間の姿勢の差分を計算する。
//   アニメーターは、再生の出来事（クリップの置き換え・再生の開始・層の添付・更新）を知らせるだけ。
//
//   - 区間は、体全体の層の、連続した再生だけをつなぐ。つながらない変化（クリップ・時間の単位・インスタンスが変わった）があれば、
//     区間を捨てて、新しい再生が始まるまで、取得を止める。
//   - 差分の計算（NRootMotion::ExtractDelta）と、姿勢の読み出し（CModelAnimationSampler）は、ゲームスレッドで、受け取るときにだけ行う。
//     Accumulate は、値を覚えるだけなので、ワーカースレッドの更新から呼べる。
//=============================================================================

// 体全体を再生している層の、ルートモーションに関わる状態（アニメーターが作って渡す）
struct FModelRootMotionPlayback
{
	bool bPlaying = false;                 // 体全体の層がある
	bool bAttachedToInstance = false;      // その層が、いまのインスタンスに付いている
	float32 UnitsPerSecondAtStart = 0.0f;  // 再生を始めたときの時間の単位
};

// 区間を作る再生の値（アニメーターが、層とクリップから作って渡す）
struct FModelRootMotionSegment
{
	int32 ClipSlot = INDEX_NONE;      // 区間を作った、体全体のクリップ
	TSharedPtr<CModel> Source;        // クリップの元（寿命を保つ）
	int32 AnimIndex = INDEX_NONE;     // Source の中のアニメーションの番号
	float32 LengthUnits = 0.0f;       // モデルの時間の単位での長さ
	float32 LengthSeconds = 0.0f;     // 秒での長さ（更新と同じ単位）
	float32 UnitsPerSecond = 0.0f;    // 再生を始めたときの時間の単位
	EAnimLoopMode Loop = EAnimLoopMode::Loop;
};

class FModelRootMotionTracker
{
public:
	// 骨を選ぶ。インスタンスと時間の単位を確かめてから。選べなければ false（選択は変えない）
	bool SelectBone( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond, const FString& BoneName );
	// 骨の選択をやめる（区間と、姿勢の読み出しの控えも捨てる）
	void ClearBone();
	bool IsBoneSelected() const { return Frame >= 0; }

	// ---- アニメーターからの知らせ ----
	// 再生中のクリップが、同じ名前で置き換えられた（ClearClips まで、取得を止める）
	void NotifyClipReplaced();
	// クリップがすべて消された
	void NotifyClipsCleared();
	// 層が、インスタンスに付いた。最初の添付先を覚え、別のインスタンスなら、取得を止める
	void NotifyLayerAttached( const TSharedPtr<CModelInstance>& Instance );
	// 体全体の再生が、新しく始まった
	void NotifyPlayStarted();

	// まだ受け取っていない区間だけを捨てる
	void ClearPending();
	// インスタンスと時間の単位が、区間を作ったときのままかを確かめる。違えば、区間を捨てて、取得を止める
	void Validate( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond );

	// 区間を足してよい状態か（骨が選ばれていて、取得が止まっていない）
	bool CanAccumulate() const;
	// つながらない再生だったことを記録する（区間を捨てて、新しい再生まで、取得を止める）
	void RejectAccumulation();
	// From から To まで進んだことを、区間に足す。前の区間とつながらなければ、区間を捨てて、取得を止める
	void Accumulate( const FModelRootMotionSegment& Segment, const TSharedPtr<CModelInstance>& Instance, float64 From, float64 To );

	// まだ受け取っていない区間の、姿勢の差分（拡大を含む affine）。区間は消さない（受け取ったら、ClearPending を呼ぶ）。
	// 区間が無い・条件が変わった・姿勢を読めないときは false（OutDelta は変えない）。ゲームスレッドから
	bool TryExtract( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond, FMatrix4x4& OutDelta );

private:
	// まだ受け取っていない、連続した再生の区間
	struct FInterval
	{
		bool bValid = false;                // From と To が同じでも、区間として扱う
		float64 From = 0.0;
		float64 To = 0.0;
		FModelRootMotionSegment Segment;
		int32 Frame = INDEX_NONE;           // 姿勢を読む骨の番号
		TWeakPtr<CModelInstance> Instance;  // 再生を始めたインスタンス
	};

	// 区間を捨てる。bRequireRestart なら、新しい再生まで、取得を止める
	void Invalidate( bool bRequireRestart );
	// インスタンスが、最初の添付先・骨を選んだときと同じかを確かめる。違えば、取得を止める
	void CheckInstance( const TSharedPtr<CModelInstance>& Instance );
	bool IsPlaybackInstanceMismatched( const TSharedPtr<CModelInstance>& Instance ) const;
	bool IsIdentityValid( const TSharedPtr<CModelInstance>& Instance ) const;

	int32 Frame = INDEX_NONE;                        // 選んだ骨の番号
	FInterval Pending;                               // 更新でまとめた、まだ受け取っていない区間
	TWeakPtr<CModelInstance> ConfiguredInstance;     // 骨を選んだときのインスタンス
	TWeakPtr<CModelInstance> PlaybackInstance;       // 最初にアニメーションを付けたインスタンス
	bool bHasPlaybackInstance = false;
	bool bRequiresRestart = false;                   // 時間の単位などが変わった。新しい再生まで、取得を止める
	bool bClipReplacementRequiresClear = false;      // 再生中のクリップが置き換えられた。ClearClips まで、取得を止める
	bool bInstanceMismatch = false;                  // インスタンスが差し替えられた。新しいアニメーターが要る
	TSharedPtr<CModelAnimationSampler> Sampler;      // 受け取るときに使う、姿勢の読み出し（使い回す）
	TSharedPtr<CModel> SamplerTarget;                // Sampler を作ったときの、対象のモデル
	TSharedPtr<CModel> SamplerSource;                // Sampler を作ったときの、アニメーションの元
	int32 SamplerAnimIndex = INDEX_NONE;             // Sampler を作ったときの、アニメーションの番号
};
