// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Asset/Model/Model.h"
#include "Library/Math/Matrix4x4.h"

// PMX のモデルを読み込み、骨をコードで動かして描く。動かし方は、時刻から決める（モーションのファイルは使わない）。
class CCharacterStage : public CGameObject
{
public:
	// モデルを読み、個体と、動かす骨の番号を用意する。
	void OnSpawn() override;
	// 時計を進めて、骨の姿勢を決める。
	void Tick( float32 DeltaTime ) override;
	// 個体を共通命令へ記録する。
	void Draw( FDrawContext& Draw ) override;

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

	TSharedPtr<CModel> Source; // 元の形状・骨・材質。
	TSharedPtr<CModelInstance> Instance; // この場面の 1 体。
	int32 Frames[NumBones]; // 骨の番号（無ければ -1）。
	FMatrix4x4 Bind[NumBones]; // 基準の姿（親基準）。
	bool bBindReady = false;
	float32 Time = 0.0f; // 動きの時計（秒）。
};
