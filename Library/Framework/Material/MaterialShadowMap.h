// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Gpu/Shader.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Types/ShaderVertex.h"
#include "Library/Math/Matrix4x4.h"

class CAssetSubsystem;

//=============================================================================
// CMaterialShadowMap: マテリアルの面に落ちる、平行光の影
//   光から見た深度を、描き込める画像（影のマップ）に描いておき、光を計算する Surface のマテリアル（DefaultLit・Substrate）が、それと比べて、影の中かを決める。
//   DxLib の影の仕組み（CShadowMap）とは別で、マテリアルのシェーダーだけで完結する（バックエンドに依らない）。
//
//   TSharedPtr<CMaterialShadowMap> Shadow = CMaterialShadowMap::Create( *Assets, "SceneShadow", 1024 );
//
//   // 毎フレーム（オブジェクトの Draw の中で）: ① 光の向きと、影を落とす範囲（中心と半径）を決める
//   Shadow->SetLight( Scene.LightDirection, FVector3( 0.0f, 0.0f, 0.0f ), 20.0f );
//   // ② 影を落とす形を、影のマップへ描く（場面を描く前に）
//   if ( Shadow->Begin( Draw ) )
//   {
//       Shadow->DrawMesh( Draw, Vertices, NumVertices, Indices, NumIndices, World );
//       Shadow->End( Draw );
//   }
//   // ③ 場面を描く: マテリアルが、影を受ける
//   Scene.ShadowMap = Shadow;
//   Material->BeginSurface( Draw, Scene );
//
//   - 範囲（中心から半径の球）の外は、影が落ちない（光が届く）。範囲が広いほど、影は粗くなる（大きさ Size の画像を、範囲の全体に使う）。
//   - Begin は、描画先を替え、奥行きの比較と書き込みを有効に、ブレンドを Opaque にする（End で、描画先は戻る。奥行きとブレンドは、戻さない）。
//   - 影を落とすのは、形の全体（マテリアルの不透明度は、見ない）。
//   - 作るのは、メインスレッドだけ（画像とシェーダーを作る）。Begin・DrawMesh・End は、記録するだけなので、どのスレッドでもよい。
//=============================================================================
class CMaterialShadowMap
{
public:
	// 作る（Size x Size の、深度を書く画像と、深度を書くシェーダー）。作れなければ null
	static TSharedPtr<CMaterialShadowMap> Create( CAssetSubsystem& Assets, const FString& Name, int32 Size = 1024 );

	// 光が進む向き（LightDirection）と、影を落とす範囲（Center から Radius の球）を決める。向きの長さが 0・半径が 0 以下なら、何もしない
	void SetLight( const FVector3& LightDirection, const FVector3& Center, float32 Radius );

	// 光のカメラの行列（ワールド → 影のマップ）
	const FMatrix4x4& GetViewProjection() const { return ViewProjection; }
	int32 GetSize() const { return Size; }
	const TSharedPtr<CRenderTarget>& GetTarget() const { return Target; }
	// 面の深度を、光のほうへずらす量（0〜1 の深度）。小さすぎると、面に縞が出る。大きすぎると、影が、落とす物から離れる
	float32 DepthBias = 0.0015f;
	// 影を調べる位置を、面の法線の向きへずらす量（影のマップの画素いくつ分か）。斜めから光が当たる面の縞を消す
	float32 NormalOffsetTexels = 1.5f;
	// NormalOffsetTexels を、ワールドの長さにしたもの
	float32 GetNormalOffset() const { return NormalOffsetTexels * 2.0f * Radius / static_cast<float32>( Size ); }

	// 影のマップへ描き始める（全体を「遠い」で消す）。画像・シェーダーが無ければ、何も記録せず false（DrawMesh・End を呼ばない）
	bool Begin( FDrawContext& Draw );
	// 影を落とす形を描く（マテリアルの DrawSurfaceMesh と同じ頂点）
	bool DrawMesh( FDrawContext& Draw, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World );
	void End( FDrawContext& Draw );

private:
	CMaterialShadowMap() = default;

	TSharedPtr<CRenderTarget> Target;
	TSharedPtr<CShader> Vertex;
	TSharedPtr<CShader> Pixel;
	FMatrix4x4 ViewProjection = FMatrix4x4::Identity;
	float32 Radius = 1.0f;
	int32 Size = 0;
};
