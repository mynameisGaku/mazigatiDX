// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Render/Scene/SceneDrawClass.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Color.h"

class CMaterial;
class CSceneDrawList;
class FShaderMeshSnapshot;
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
	// 定義から固定した分類を返す。未作成・手動指定ではOutを変更せずfalse。
	bool TryGetDrawClass( ESceneDrawClass& Out ) const;
	// 固定した設定と一つのメッシュを所有する命令列へする。失敗時はOutを変更しない。
	bool BuildMeshCommands( FDrawBuffer& Out, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
	// 不透明・切り抜きは深度書き込み、半透明は通常アルファ合成と読み取り深度で追加する。
	// 命令と固定資源の所有を一緒に受け付け、失敗時は一覧を変更しない。
	bool AddMesh( CSceneDrawList& List, ESceneDrawClass Class, const FVector3& WorldCenter, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
	// 固定した形状を参照して個体定数だけを記録する。不正な値では一覧を変更しない。
	bool AddMesh( CSceneDrawList& List, ESceneDrawClass Class, const FVector3& WorldCenter, const FShaderMeshSnapshot& Mesh, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
	// 定義の分類から深度・合成・順序を選ぶ。分類未指定ならfalse。
	bool AddMesh( CSceneDrawList& List, const FVector3& WorldCenter, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
	// 共有形状を使い、定義の分類から深度・合成・順序を選ぶ。
	bool AddMesh( CSceneDrawList& List, const FVector3& WorldCenter, const FShaderMeshSnapshot& Mesh, const FMatrix4x4& World, const FColor32& Tint = FColor32::White ) const;
private:
	// 明示した分類が定義と一致するか、手動指定で使えるかを調べる。
	bool AcceptsDrawClass( ESceneDrawClass Class ) const;
	friend class CMaterial;
	TSharedPtr<const FMaterialSurfaceSnapshotData> Data; // 元の材質から独立した定数と資源参照。
};
