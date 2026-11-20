// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Core/Render/Scene/SceneDrawClass.h"
#include "Library/Framework/Render/Mesh/ShaderMeshSnapshot.h"

// 記録中だけ変更でき、確定後は読み取りだけにする内部データ。
struct FSceneDrawSnapshotData
{
	// 一回のExecuteで状態を閉じる描画項目。
	struct FItem
	{
		TArray<uint32> Words; // カメラ・状態・描画の所有コピー。
		FShaderMeshSnapshot ShaderMesh; // 参照命令が使う形状の共有所有。
		ESceneDrawClass Class = ESceneDrawClass::Opaque; // この項目の分類。
		float32 Depth = 0; // カメラの前方向に測った奥行き。
		int32 Ordinal = 0; // 合流した順序を含む元の順序。
	};
	FCamera Camera; // 記録時に固定したカメラ。
	TArray<uint32> CameraWords; // カメラ一致の比較と各項目の先頭に使う命令。
	TArray<FItem> Items; // 受付順、確定後は描画順の項目。
	TArray<TSharedPtr<const uint8>> Owners; // 不透明な値として保持する既存の共有所有。
	int32 NumWords = 0; // 各項目が所有する合計word数。
};
