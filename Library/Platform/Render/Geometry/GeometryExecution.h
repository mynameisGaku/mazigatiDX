// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Platform/Render/Geometry/VertexLighting.h"

// 呼び出した命令を扱ったか。未対応・不正はSpanを中止するため区別する。
enum class EGeometryCommandResult : uint8
{
	NotHandled, // この実行側の命令ではない。
	Executed, // 状態を更新または描画を記録した。
	Rejected, // 不正・未対応・GPU準備の失敗。
};

// 3Dの命令の実行が共有する、このSpanだけの状態（ワールド・カメラ・深度と表裏・ライト・フォグ・影・借りた実行先）。役割ごとのクラスが、これを共有する。
struct FGeometryExecution
{
	// Geometry・Textures・Buffers・Models は借用。同期実行中だけ生存させる。Geometry が nullptr でも状態命令は読めるが、描画は失敗する。
	FGeometryExecution( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels );

	ITextureDrawSource* Textures; // 登録済み画像を同期実行中だけ借りる。
	IStaticModelSource* Models; // 同期実行中だけ借りる静的モデル。
	IGeometryBufferSource* Buffers; // 登録済みのローカル形状を借りる。
	IRenderGeometry3D* Geometry; // 借用するGPU実行先。
	FMatrix4x4 World; // 現在のワールド行列。
	FCamera Camera; // 現在のカメラ。
	FGeometryDrawState State; // 現在の深度と表裏。
	CVertexLighting Lighting; // このSpanだけのライティング（光）の状態。
	FDrawFogData Fog; // このSpanだけの距離フォグ（Modeが0なら、なし）。
	FDrawFogData HeightFog; // このSpanだけの高さフォグ（Modeが0なら、なし）。
	FDrawTextureTransformData TextureTransform; // このSpanだけの画像の変換（Enabledが0なら、なし）。
	bool bAnisotropicFilter = false; // 画像の補間が、異方性か。
	int32 AnisotropyLevel = 16; // 異方性の最大の強さ（1 〜 16）。
	IShaderDrawSource* ShaderSource = nullptr; // 影の描画に使うシェーダーの実行先（借用）。
	bool bUseForwardPath = true; // GPU が変換と光を計算する経路を使うか（比べるためと、検査のために切れる）。
	bool bShadowCasting = false; // 影の絵を描いているか。
	FMatrix4x4 LightViewProjection; // 影の絵を描くときの、光から見た行列。
	int32 ShadowSlotMaps[3] = { -1, -1, -1 }; // スロットごとの、使っている影の絵。
	int32 ReceiverSlot = -1; // 影を受けるスロット（-1 は、受けない）。
	TArray<FMatrix4x4> WorldStack; // このSpanだけの変換の積み重ね。
	TArray<FCamera> CameraStack; // このSpanだけのカメラの積み重ね。
};
