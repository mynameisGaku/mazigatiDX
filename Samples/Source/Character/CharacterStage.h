// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Asset/Model/Model.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Framework/Render/Scene/SceneDrawSnapshot.h"

// PMX のモデルを読み込み、骨と表情（形状）をコードで動かして描く。動かし方は、時刻から決める（モーションのファイルは使わない）。
class CCharacterStage : public CGameObject
{
public:
	// モデルを読み、個体と、動かす骨の番号を用意する。
	void OnSpawn() override;
	// 時計を進めて、骨の姿勢を決める。
	void Tick( float32 DeltaTime ) override;
	// 個体を共通命令へ記録する。
	void Draw( FDrawContext& Draw ) override;
	// 固定姿勢の比較を選んだ場合に、保持している一覧を再生する。
	void DrawFrozen( IDrawExecutor& Executor ) const;
	// 固定姿勢を表示する比較モードかを返す。
	bool HasFrozenPose() const { return Frozen.IsValid(); }

private:
	// 動かす骨（名前で探す。PMX の標準の骨の名前）。
	enum class EBone : uint8
	{
		Center,
		UpperBody,
		UpperBody2,
		Neck,
		Head,
		LeftArm,
		LeftElbow,
		RightArm,
		RightElbow,
		LeftEar,
		RightEar,
		TailCenter1,
		TailCenter2,
		TailLeft1,
		TailLeft2,
		TailRight1,
		TailRight2,
		Count,
	};
	static constexpr int32 NumBones = static_cast<int32>( EBone::Count );

	// 骨を、回転（親から見た軸のまわり）だけ動かす。基準の姿は、読み込んだままの親基準の行列。
	void Rotate( EBone Bone, float32 X, float32 Y, float32 Z );
	// 骨を、基準の姿から、平行移動だけずらす。
	void Move( EBone Bone, const FVector3& Offset );
	// 基準の姿（親基準の行列）を、全部の骨で読めたか。読めるまで、毎回試す（モデルの準備が終わっていないことがある）。
	bool CaptureBindPose();

	FSceneDrawSnapshot Frozen; // 個体を手放した後にも描ける記録。
	TSharedPtr<CModel> Source; // 元の形状・骨・材質。
	TSharedPtr<CModelInstance> Instance; // この場面の 1 体。
	TArray<TSharedPtr<CModelInstance>> Crowd; // 計測用（--character-copies=<N>）: 基準の姿のまま、格子に並べる複製。同じモデルの個体は、GPU のメッシュを共有する。
	int32 Frames[NumBones]; // 骨の番号（無ければ -1）。
	FMatrix4x4 Bind[NumBones]; // 基準の姿（親基準）。
	bool bBindReady = false;
	int32 BlinkShape = -1; // 表情（形状）の番号: まばたき（無ければ -1）。
	int32 SmileShape = -1; // 表情（形状）の番号: にこり（無ければ -1）。
	float32 Time = 0.0f; // 動きの時計（秒）。
};
