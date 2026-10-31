// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// FDxLibShader
//   1 回の Execute の間に行った、シェーダーの設定（使わせた頂点シェーダー・ピクセルシェーダー、置いた画像、置いた定数バッファ）を覚えて、終わりに元に戻す。
//   シェーダーで描く命令（PrimitiveShader3D / ShaderRect）も、ここで実行する。
//
//   DxLib の実測（Direct3D 11）:
//   - シェーダーで描く命令（Draw...ToShader）は、頂点シェーダーとピクセルシェーダーの両方が決まっていないと、何も描かない。ふつうの図形・モデルの描画には、影響しない
//   - 頂点シェーダーの入力は、宣言した順に、詰めて読まれる（FShaderVertex3D の並びのとおりに宣言する）
//   - 2D のシェーダーの描画（DrawPrimitive2DToShader）は、頂点シェーダーの中身を使わず、DxLib 自身の頂点処理で描く（実測）ので、使わない。
//     ShaderRect は、組み込みの頂点シェーダー（クリップ座標をそのまま渡す）と、3D のシェーダーの描画で、矩形を描く
//   - 定数バッファのスロットは、HLSL の register( bN ) の N。0〜3 は DxLib 自身が使う（上書きすると、普通の 3D の描画が壊れる）ので、4〜13 だけを使わせる。
//     中身は、書き換えて UpdateShaderConstantBuffer したものが、そのあとに描くものに効く（同じフレームの中で、描く順に替えられる）
//   - 画像は、HLSL の Texture2D を t0 から連続して宣言する。t1 だけを宣言すると、DxLib が落ちる（なので、段は実行側では確かめられない。呼び出す側の約束）
//   - NULL の頂点配列・負のポリゴン数を渡すと DxLib が落ちるので、渡す前にすべて確かめる（記録のときと、実行のとき）
//
//   Apply: シェーダーの命令（UseShader / ShaderConstants / ShaderTexture / PrimitiveShader3D / ShaderRect）を実行する
//   Restore: 使わせたシェーダーと画像を外し、使った定数バッファを（中身を 0 にして）片付ける
//=============================================================================
class FDxLibShader
{
public:
	// 状態だけを変える命令か（クリップ範囲が空のあいだも実行する）
	static bool IsStateCommand( EDrawCommand Type );

	// 影の絵を描いている間は実行しない命令か（シェーダーで描く命令。影の絵に、自分のシェーダーで描いてしまわないように）
	static bool IsBlockedInPass( EDrawCommand Type );

	// 命令を実行する。シェーダーの命令でなければ、何もせず false
	bool Apply( const FDrawCommandView& Command );

	// 使わせたシェーダーと画像を外し、使った定数バッファを片付ける
	void Restore();

private:
	// 命令ごとの実行
	void UseShader( const FDrawCommandView& Command );
	void SetConstants( const FDrawCommandView& Command );
	void SetTexture( const FDrawCommandView& Command );
	void DrawPrimitive3D( const FDrawCommandView& Command );
	void DrawRect( const FDrawCommandView& Command );

	// 段 Stage を空にする（空の画像を置く）
	static void SetEmptyTexture( int32 Stage );

	// 頂点シェーダーとピクセルシェーダーが、両方決まっているか
	bool CanDraw() const { return VertexShader >= 0 && PixelShader >= 0; }

	// 使わせている DxLib のシェーダー（負なら、使わせていない）
	int VertexShader = -1;
	int PixelShader = -1;

	// SetUseTextureToShader で画像を置いた段（終わりに外す）
	bool bTextureUsed[NDrawShader::NumTextureStages] = {};

	// 使っている定数バッファ（種類とスロットごとに 1 つ。大きさが足りなくなったら作り直す）
	struct FConstantBuffer
	{
		int32 Stage = 0;
		int32 Slot = 0;
		int Buffer = -1;
		int32 NumBytes = 0;
	};
	TArray<FConstantBuffer> ConstantBuffers;
};
