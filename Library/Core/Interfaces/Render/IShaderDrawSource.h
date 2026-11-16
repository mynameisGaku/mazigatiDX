// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/RenderTargetFormat.h"
#include "Library/Math/Matrix4x4.h"

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
	ShadowLit, // 影を受ける光の計算: SubPosition を位置にして、色（Diffuse = 影の外の色、Specular = 影の中で消える色）・画像の座標・光から見た位置（Normal）を渡す。
	EffectMaterial, // 材質付き 3D 描画: SubPosition を位置にして、色（Diffuse）・画像の座標・点ごとの材質の値（Normal = 消失・発光・ゆがみの強さ、Tangent.xy = 画像の移動、Binormal.xy = ゆがみ画像の移動）を渡す。
	PixelLit, // 画素ごとの光: SubPosition を位置にして、図形の色（Diffuse / Specular）・画像の座標・世界の位置（Position）・世界の法線（Normal）を渡す。
};

// 組み込みのピクセルシェーダー（影のための描画。利用者のピクセルシェーダーの代わり）
enum class EBuiltInPixelShader : uint8
{
	None,               // 利用者のピクセルシェーダーを使う。
	ShadowDepth,        // 影の絵へ、光から見た深度を書く（Quad3D の頂点シェーダーと組み合わせる。出力は R32Float の 1 つ）。
	ShadowLit,          // 影を受けて色を決める（ShadowLit の頂点シェーダーと組み合わせる。t1 = 影の絵、定数 b4 = ( 影の絵の幅, 高さ, 補正の深さ, 0 )）。
	ShadowLitTextured,  // 同じ。さらに、t0 の画像の色を掛ける（画像と頂点の色の積のアルファが 0 なら、描かない）。
	FogTextured,        // t0 の画像に頂点の色（Diffuse）を掛け、フォグへ混ぜる（ShadowLit の頂点シェーダーと組み合わせる。Specular の RGB = フォグの色、A = 隠れない割合）。
	ImageFilter,        // 画像のフィルター（Rect の頂点シェーダーと組み合わせる。t0 = 元の画像、t1 = グラデーションマップ、定数 b5 = 種類と引数）。画素の座標で、元の画像を読む。
	PixelLit,           // 画素ごとの光（PixelLit の頂点シェーダーと組み合わせる。定数 b6 = 光・材質・カメラ）。光の式は、CVertexLighting と同じ。
	PixelLitTextured,   // 同じ。さらに、t0 の画像の色を掛ける（アルファが 0 なら、描かない）。
	EffectMaterial,     // 材質付き 3D 描画（EffectMaterial の頂点シェーダーと組み合わせる。t0 = 表示画像、t1 = 消失のマスク、t2 = ゆがみの画像、定数 b7 = 材質の設定）。画像・消失・境界の発光・UV のゆがみ。
	ImageBlend,         // 2 枚の画像の混ぜ合わせ（Rect の頂点シェーダーと組み合わせる。t0 = 元の画像、t1 = 混ぜる画像、定数 b5 = 種類と引数）。
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
	int32 PixelShader = -1;                           // 利用者のピクセルシェーダーの番号（BuiltinPixel が None のとき）。
	EBuiltInPixelShader BuiltinPixel = EBuiltInPixelShader::None;
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
	// 影の絵（シャドウマップ）を描いたときの、光から見た行列・補正の深さ・大きさを覚える（影を受ける描画が、あとで引く）。
	virtual void SetShadowPass( int32 /*ShadowMap*/, const FMatrix4x4& /*LightViewProjection*/, float32 /*AdjustDepth*/, int32 /*Width*/, int32 /*Height*/ ) {}
	// 覚えてある、最後の影の絵の値。まだ描いていないときは false。
	virtual bool GetShadowPass( int32 /*ShadowMap*/, FMatrix4x4& /*OutLightViewProjection*/, float32& /*OutAdjustDepth*/, int32& /*OutWidth*/, int32& /*OutHeight*/ ) const { return false; }
};
