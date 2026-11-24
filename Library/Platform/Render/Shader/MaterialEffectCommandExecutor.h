// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Framework/Render/Draw/DrawMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawDistortedMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawMaterialMesh.h"
#include "Library/Framework/Render/Draw/DrawModelMaterial.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Platform/Render/Shader/ShaderCommandExecutor.h"

class CGeometryCommandExecutor;
struct FDrawMaterialQuadData;
struct FDrawDistortedMaterialQuadData;
struct FModelMaterialGeometry;

//=============================================================================
// CMaterialEffectCommandExecutor
//   材質付きの 3D 描画（MaterialQuad3D / DistortedMaterialQuad3D / MaterialRibbon3D / DistortedMaterialRibbon3D / MaterialMesh3D / ModelMaterial3D）を、共通の描画命令から解釈する。
//   画像・消失マスク・境界の発光・UV のゆがみを、組み込みのシェーダー（EBuiltInVertexShader::EffectMaterial / EBuiltInPixelShader::EffectMaterial）で描く。
//   式は、DxLib の実装（EffectMaterialRenderer）と同じ。描画 API を知らない（頂点と定数を作って、シェーダーの実行先へ渡す）。
//   合成は、Alpha と Add だけ（DxLib と同じ。ほかの合成は、描かない）。
//=============================================================================
class CMaterialEffectCommandExecutor
{
public:
	// Geometry と Shader は借用する。
	CMaterialEffectCommandExecutor( CGeometryCommandExecutor& InGeometry, CShaderCommandExecutor& InShader );

	// 材質付き 3D 描画の命令か。
	static bool IsMaterialEffectCommand( EDrawCommand Type );
	// 1 命令を検査して実行する。Bright・Alpha・bAlphaBlend は、いまの描画の状態。
	CShaderCommandExecutor::EResult Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// ワールド座標（World があれば、それを掛ける前のローカル座標）の三角形を、材質つきで描く。Opacity は、頂点のアルファへ掛ける。bTwoSided が false なら、いまの表裏の設定を使う。
	bool DrawMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, const FMatrix4x4* World, float32 Opacity );

private:
	// 材質の設定（四角形・帯で共通）。
	struct FEffectSettings
	{
		int32 SourceTexture = -1;              // 表示する画像。
		int32 MaskTexture = -1;                // 消失のマスク画像。負なら、組み込みの模様。
		int32 DistortionTexture = -1;          // ゆがみの画像。負なら、ゆがめない。
		float32 UVScale[2] = { 1.0f, 1.0f };   // 表示画像の倍率。
		float32 MaskScale = 1.0f;              // マスクの倍率。
		float32 EdgeWidth = 0.0f;              // 消失境界の幅。
		float32 EdgeEmission = 0.0f;           // 消失境界の発光倍率。
		FDrawRgba EdgeColor;                   // 消失境界の色。
		bool bDissolve = false;                // 消失と境界発光を使うか。
		bool bDepthTest = true;                // 深度を比べるか。
		bool bDepthWrite = false;              // 深度を書くか。
		float32 DistortionScale[2] = { 1.0f, 1.0f };   // ゆがみ画像の倍率。
	};
	// 頂点に、点ごとの材質の値を入れる（Normal = 消失・発光・ゆがみの強さ、Tangent = 画像の移動、Binormal = ゆがみ画像の移動）。
	static void SetVertexValues( FDrawShaderVertex3D& Vertex, float32 Dissolve, float32 Emission, float32 Strength, const float32* UVOffset, const float32* DistortionOffset );
	// 四角形を描く。Distortion が nullptr なら、ゆがめない。
	bool DrawQuad( const FDrawMaterialQuadData& Data, const FDrawDistortedMaterialQuadData* Distortion, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// 帯を描く。Distortion と DistortionPoints は、ゆがむときだけ（どちらも nullptr か、どちらも有効）。
	bool DrawRibbon( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points, const FDrawDistortedMaterialRibbonData* Distortion, const FDrawDistortedMaterialRibbonData::FPoint* DistortionPoints, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// モデルの一材質だけに材質効果をかける。選んだ材質のメッシュを隠して、残りを通常に描き、そのあと、選んだ面を材質つきで描く。
	bool DrawModelMaterial( const FDrawModelMaterialData& Data, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// 現在の姿勢の材質範囲ごとに通常描画と効果を分ける。
	bool DrawModelMaterialSections( const FDrawModelMaterialData& Data, const FModelMaterialGeometry& Shape, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// 頂点（クリップ座標つき、三角形のリスト）を、設定と定数をつけて描く。
	bool DrawVertices( const FEffectSettings& Settings, const TArray<FDrawShaderVertex3D>& Vertices, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, bool bTwoSided = true );

	CGeometryCommandExecutor& Geometry;
	CShaderCommandExecutor& Shader;
};
