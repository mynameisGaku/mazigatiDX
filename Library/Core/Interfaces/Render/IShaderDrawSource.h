// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/RenderTargetFormat.h"

//=============================================================================
// 利用者のシェーダーで描く、1 回の描画の値と、それを実行する窓口
//   シェーダーの所有・コンパイル・パイプラインの作成は、描画 API の実装が持つ。描画の命令を解釈する側（ShaderCommandExecutor）は、
//   この窓口へ、シェーダーの番号・定数・画像の番号・頂点を渡すだけで、描画 API を知らない。ゲームスレッドから呼ぶ。
//=============================================================================

// 組み込みの頂点シェーダー（利用者が頂点シェーダーを用意しなくてよい描画）
enum class EBuiltInVertexShader : uint8
{
	None,    // 利用者の頂点シェーダーを使う。
	Rect,    // ShaderRect: 頂点の位置（クリップ座標）をそのまま渡し、色・画像の座標・Extra をピクセルシェーダーへ渡す。
	Quad3D,  // ShaderQuad3D: 頂点の SubPosition（クリップ座標 x, y, z, w）を位置にして、同じものを渡す。
};

// 定数バッファの中身（シェーダーの cbuffer register( bN ) の N = Slot）
struct FShaderConstantData
{
	int32 Stage = 0;                  // EShaderStage の数値。
	int32 Slot = 0;                   // 4〜13。
	TArrayView<const uint8> Bytes;    // 中身（16 の倍数）。呼び出しの間だけ有効。
};

// 1 回の描画。配列は、呼び出しの間だけ借りる。
struct FShaderDrawRequest
{
	// 頂点 1 つのバイト数（FDrawShaderVertex3D = 88 バイト）。
	static constexpr int32 VertexStride = 88;
	// 画像の段の数（t0〜t15）。
	static constexpr int32 NumTextureStages = 16;

	int32 VertexShader = -1;                          // 利用者の頂点シェーダーの番号（Builtin が None のとき）。
	int32 PixelShader = -1;                           // 利用者のピクセルシェーダーの番号。
	EBuiltInVertexShader Builtin = EBuiltInVertexShader::None;
	FGeometryDrawState State;                         // 深度・表裏・合成・補間・画像の外側。Topology は、下の Topology を使う。
	EGeometryTopology Topology = EGeometryTopology::Triangles;
	bool bUseDepth = true;                            // false なら、深度を使わない（画面の矩形など）。
	int32 NumTargets = 1;                             // 同時に書く色の描画先の数（1〜8。ピクセルシェーダーの SV_Target の数と合わせる）。
	ERenderTargetColorFormat TargetFormats[8] = { ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8,
												  ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8 };   // 描画先ごとの画素形式。
	const uint8* Vertices = nullptr;                  // 頂点（VertexStride バイトずつ）。
	int32 NumVertices = 0;
	const uint32* Indices = nullptr;                  // 添字（任意）。nullptr なら、頂点を順に使う。
	int32 NumIndices = 0;
	TArrayView<const FShaderConstantData> Constants;  // 置いてある定数。
	int32 Textures[NumTextureStages] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // 段ごとの画像の番号（-1 は、空）。
};

class IShaderDrawSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IShaderDrawSource() = default;
	// 番号が、その種類の、作ってあるシェーダーか。
	virtual bool IsShaderValid( int32 Shader, EShaderStage Stage ) const = 0;
	// 描く。シェーダーの組み合わせが合わない・パイプラインを作れない・GPU に渡せないときは false（何も描かない）。
	virtual bool DrawShader( const FShaderDrawRequest& Request, IRenderGeometry3D& Geometry ) = 0;
};
