// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

// サンプル Deferred（Samples/Source/Deferred）の、7 つのシェーダー
enum class EDeferredSampleShader : uint8
{
	Vertex,         // 場面の頂点シェーダー（Forward と ジオメトリのパスで共通）
	ForwardObject,  // Forward 経路のピクセルシェーダー: 物
	ForwardGround,  // Forward 経路のピクセルシェーダー: 地面
	GeometryObject, // ジオメトリのパスのピクセルシェーダー: 物（G-buffer の 4 つの出力へ書く）
	GeometryGround, // ジオメトリのパスのピクセルシェーダー: 地面
	Lighting,       // ライティングのパスのピクセルシェーダー（G-buffer を読んで光を当てる）
	Composite,      // 場面の色を、露出とトーンマップを掛けて、画面へ貼るピクセルシェーダー
};

//=============================================================================
// NDeferredShaders
//   Deferred 描画の HLSL の正本（EngineContent/Shaders/Deferred）を、文字列として渡す。HLSL は、C++ に埋め込まず、EngineContent/Shaders のファイルとして書く。
//   戻り値は、プログラムの終わりまで有効な、NUL で終わる文字列（GetSampleSource は、連ねた文字列を返す）。
//=============================================================================
namespace NDeferredShaders
{
	// G-buffer へ書く（GBufferOutput と PackGBuffer）。ジオメトリのパスのピクセルシェーダーの先頭へ足す
	const char* GetGBufferWriteSource();

	// G-buffer を読む（画像の段 t0〜t3・サンプラー s0・FGBufferView の b6、LoadGBuffer と ReconstructWorldPosition）。ライティングのパスのピクセルシェーダーの先頭へ足す
	const char* GetGBufferReadSource();

	// サンプル Deferred のシェーダーの全文（必要な共通の部品を連ねたもの。そのまま CShader::Compile に渡せる。入口は main）。
	// 定数バッファの並びは、Samples/Source/Deferred/DeferredSources.h の構造体と同じ
	FString GetSampleSource( EDeferredSampleShader Shader );
}
