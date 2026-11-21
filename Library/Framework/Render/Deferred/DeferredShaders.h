// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// NDeferredShaders
//   Deferred 描画の HLSL の正本（EngineContent/Shaders/Deferred）を、文字列として渡す。CShader::Compile へ渡す前に、各自のシェーダーの先頭へ足す。
//   HLSL は、C++ に埋め込まず、EngineContent/Shaders のファイルとして書く。ここにあるのは、製品の部品（G-buffer の書き出しと読み出し）だけで、
//   サンプルだけが使うシェーダーは、サンプルの素材（Samples/Assets/Deferred）に置いて、実行時に読む。
//   戻り値は、プログラムの終わりまで有効な、NUL で終わる文字列。
//=============================================================================
namespace NDeferredShaders
{
	// G-buffer へ書く（GBufferOutput と PackGBuffer）。ジオメトリのパスのピクセルシェーダーの先頭へ足す
	const char* GetGBufferWriteSource();

	// G-buffer を読む（画像の段 t0〜t3・サンプラー s0・FGBufferView の b6、LoadGBuffer と ReconstructWorldPosition）。ライティングのパスのピクセルシェーダーの先頭へ足す
	const char* GetGBufferReadSource();
}
