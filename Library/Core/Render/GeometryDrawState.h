// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/TextureDrawView.h"
#include "Library/Core/Types/TypeDefinition.h"

// 頂点をリスト形式でつなぐ方法。帯・扇は共通処理でリストへ展開する。
enum class EGeometryTopology : uint8
{
	Points, // 1頂点ずつの点。
	Lines, // 2頂点ずつの線分。
	Triangles, // 3頂点ずつの三角形。
};

// 描く深度と、描画先の深度との比較。
enum class EGeometryCompare : uint8
{
	Never, // 常に拒否。
	Less, // 小さい場合。
	Equal, // 等しい場合。
	LessEqual, // 以下の場合。
	Greater, // 大きい場合。
	NotEqual, // 異なる場合。
	GreaterEqual, // 以上の場合。
	Always, // 常に通す。
};

// 表は画面上で時計回り。描画API固有の表裏定義は実装側で合わせる。
enum class EGeometryCulling : uint8
{
	None, // 両面。
	BackFaces, // 裏面を除去。
	FrontFaces, // 表面を除去。
};

// 画像の外側の参照方法。API固有の値は実装側が変換する。
enum class EGeometryTextureAddress : uint8
{
	Wrap, // 繰り返す。
	Mirror, // 折り返して繰り返す。
	Clamp, // 端の画素を伸ばす。
	Border, // 外側は透明な黒。
};

// 描く色と、描画先にある色の混ぜ方（bAlphaBlendがtrueのときに使う）。色は非乗算済み（Premultiplied以外）。
enum class EGeometryBlend : uint8
{
	Alpha, // 描く色 x アルファ ＋ 描画先 x ( 1 - アルファ )。
	Add, // 描画先 ＋ 描く色 x アルファ。
	Subtract, // 描画先 － 描く色 x アルファ。
	Multiply, // 描画先 x 描く色。
	PremultipliedAlpha, // 描く色 ＋ 描画先 x ( 1 - アルファ )。
	PremultipliedAdd, // 描画先 ＋ 描く色。
	PremultipliedSubtract, // 描画先 － 描く色。
	Custom, // 利用者が決めた係数（FGeometryDrawState::CustomBlend）。
};

// 混ぜ方の係数（描く色・描画先の色に掛ける値）。
enum class EGeometryBlendFactor : uint8
{
	Zero, // 0。
	One, // 1。
	SrcColor, // 描く色。
	InvSrcColor, // 1 － 描く色。
	SrcAlpha, // 描く色のアルファ。
	InvSrcAlpha, // 1 － 描く色のアルファ。
	DestColor, // 描画先の色。
	InvDestColor, // 1 － 描画先の色。
	DestAlpha, // 描画先のアルファ。
	InvDestAlpha, // 1 － 描画先のアルファ。
	SrcAlphaSaturate, // min( 描く色のアルファ, 1 － 描画先のアルファ )。
};

// 係数をどう組み合わせるか。
enum class EGeometryBlendOp : uint8
{
	Add, // 描く色 x 係数 ＋ 描画先 x 係数。
	Subtract, // 描く色 x 係数 － 描画先 x 係数。
	ReverseSubtract, // 描画先 x 係数 － 描く色 x 係数。
	Min, // 小さいほう。
	Max, // 大きいほう。
};

// 利用者が決める混ぜ方（Blend が Custom のとき使う）。色とアルファを別々に決める。
struct FGeometryCustomBlend
{
	EGeometryBlendFactor SrcColor = EGeometryBlendFactor::One;
	EGeometryBlendFactor DestColor = EGeometryBlendFactor::Zero;
	EGeometryBlendOp ColorOp = EGeometryBlendOp::Add;
	EGeometryBlendFactor SrcAlpha = EGeometryBlendFactor::One;
	EGeometryBlendFactor DestAlpha = EGeometryBlendFactor::Zero;
	EGeometryBlendOp AlphaOp = EGeometryBlendOp::Add;

	// 値が、決められた範囲に収まっているか。
	bool IsValid() const
	{
		return SrcColor <= EGeometryBlendFactor::SrcAlphaSaturate && DestColor <= EGeometryBlendFactor::SrcAlphaSaturate && SrcAlpha <= EGeometryBlendFactor::SrcAlphaSaturate
			&& DestAlpha <= EGeometryBlendFactor::SrcAlphaSaturate && ColorOp <= EGeometryBlendOp::Max && AlphaOp <= EGeometryBlendOp::Max;
	}
	// 6つの値を1つの整数にまとめる（保管のキーに使う）。
	uint32 Pack() const
	{
		return static_cast<uint32>( SrcColor ) | ( static_cast<uint32>( DestColor ) << 4 ) | ( static_cast<uint32>( ColorOp ) << 8 ) | ( static_cast<uint32>( SrcAlpha ) << 12 ) | ( static_cast<uint32>( DestAlpha ) << 16 )
			| ( static_cast<uint32>( AlphaOp ) << 20 );
	}
};

// 2Dの描画を、画素ごとに、画像の値で薄める設定（PushMask）。画像は、描画先の左上に合わせて読む（画像の外は、薄めない）。
struct FGeometryMask
{
	FTextureDrawView Texture; // マスクの画像。Binding が 0 なら、マスクを使わない。
	int32 Channel = 0; // 値にする成分（0 = アルファ、1 = 赤、2 = 緑、3 = 青）。
	bool bInvert = false; // true なら、値が小さいところほど濃く描く。
	bool IsActive() const { return Texture.Binding != 0; }
};

// 1回の描画に閉じたGPU状態。ほかの命令へ持ち越さない。
struct FGeometryDrawState
{
	EGeometryTopology Topology = EGeometryTopology::Triangles; // 頂点のつなぎ方。
	EGeometryCompare Compare = EGeometryCompare::LessEqual; // 深度の比較。
	EGeometryCulling Culling = EGeometryCulling::None; // 除去する面。
	bool bTestDepth = true; // 深度を比べるか。
	bool bWriteDepth = true; // 通過した深度を書くか。
	bool bAlphaBlend = false; // 描画先の色と混ぜるか（falseなら上書き）。
	EGeometryBlend Blend = EGeometryBlend::Alpha; // 混ぜ方。bAlphaBlendがtrueのときだけ使う。
	FGeometryCustomBlend CustomBlend; // Blendが Custom のときの係数。
	EGeometryTextureAddress AddressU = EGeometryTextureAddress::Wrap; // 横方向の画像の外側。
	EGeometryTextureAddress AddressV = EGeometryTextureAddress::Wrap; // 縦方向の画像の外側。
	bool bLinearFilter = false; // falseなら最近傍、trueなら線形補間。
	int32 DepthBias = 0; // GPUの整数深度偏り。
	FGeometryMask Mask; // 2Dの描画を薄めるマスク。3Dは使わない。
	uint8 MaxAnisotropy = 1; // 異方性補間の最大の強さ（1 〜 16。1 はなし）。画像の補間を線形で使うときだけ効く。
};
