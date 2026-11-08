// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Material/MaterialPreviewMesh.h"

class IDrawExecutor;
class CRenderTarget;

// プレビューで見せる形（Surface のマテリアル）
enum class EMaterialPreviewShape : uint8
{
	Sphere,
	Cube,
	Plane
};

//=============================================================================
// FMaterialPreviewSurface
//   マテリアルエディタのプレビュー。ツール用の描画先（CRenderTarget）を持ち、マテリアルを、ドメインに合わせて描く。メインスレッドだけで使う。
//     Surface      球・立方体・板（NMaterialPreview）を、軌道カメラと、決まった光で
//     UI・Particle 描画先いっぱいの矩形（後ろに市松模様。半透明が見える）
//     PostProcess  決まった場面（色の帯と勾配）を、別の描画先へ描いて、それを読んで描く
//   描くのは、フレームワークの描画先とコマンドリストだけ。DxLib を直接は呼ばない。
//=============================================================================
class FMaterialPreviewSurface
{
public:
	static constexpr int32 Size = 256;

	FMaterialPreviewSurface() = default;
	~FMaterialPreviewSurface();
	FMaterialPreviewSurface( const FMaterialPreviewSurface& ) = delete;
	FMaterialPreviewSurface& operator=( const FMaterialPreviewSurface& ) = delete;

	// 描く。描画先を確保できない・マテリアルが無効・シェーダーが作れないなら false（前の絵は残す）
	bool Render( CMaterial& Material, EMaterialPreviewShape Shape, float32 Yaw, float32 Pitch, const FMaterialFrameState& Frame, IDrawExecutor& Executor );
	void Release();
	// 表示用の画像の番号（ImGui に渡す）。まだ確保していなければ -1
	int32 GetHandle() const;

private:
	bool EnsureTargets();

	TSharedPtr<CRenderTarget> Target;
	TSharedPtr<CRenderTarget> SceneTarget;
	FMaterialPreviewMesh Sphere;
	FMaterialPreviewMesh Cube;
	FMaterialPreviewMesh Plane;
};
