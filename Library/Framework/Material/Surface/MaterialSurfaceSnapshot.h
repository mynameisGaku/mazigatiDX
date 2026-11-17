// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Render/Scene/SceneDrawClass.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Color.h"

class CMaterial;
class CSceneDrawList;
struct FDrawBuffer;
struct FShaderVertex3D;
struct FMaterialSurfaceSnapshotData;

// 材質の定数・画像・シェーダーを固定した不変の値。公開後はCMaterialへ触れず並列に記録できる。
class FMaterialSurfaceSnapshot
{
public:
	// 既定値は未作成。
	FMaterialSurfaceSnapshot() = default;
	// Surfaceの準備と固定が成功した値かを返す。
	bool IsValid() const;
	// 固定した設定と一つのメッシュを所有する命令列へする。失敗時はOutを変更しない。
	bool BuildMeshCommands( FDrawBuffer& Out, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
	// 不透明は深度書き込み、半透明は通常アルファ合成と読み取り深度で追加する。切り抜きは未対応。
	// 命令と固定資源の所有を一緒に受け付け、失敗時は一覧を変更しない。
	bool AddMesh( CSceneDrawList& List, ESceneDrawClass Class, const FVector3& WorldCenter, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
private:
	friend class CMaterial;
	TSharedPtr<const FMaterialSurfaceSnapshotData> Data; // 元の材質から独立した定数と資源参照。
};
