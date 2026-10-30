// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// NDxLibRenderState3D
//   フレームワークが DxLib の初期化で決める、3D の描画状態の初期値。
//   DxLib には、深度バッファの使い方・画像の貼り方の取得関数が無いので、描画（Execute）の終わりに戻す先を、ここで持つ。
//   取得関数があるもの（カリング・フォグ・異方性）は、変える前の値を DxLib から読んで戻す。
//=============================================================================
namespace NDxLibRenderState3D
{
	// 比べ方（アルファテストと深度バッファで共通）を DxLib の DX_CMP_* にする
	int ToDxCompare( int32 Compare );

	// 深度バッファを使う設定にする（DxLib の初期化のあとに 1 度呼ぶ。以降、描画の終わりに戻す先になる）。
	// 比べ方は LessEqual、偏りは 0、画像の外側の貼り方は繰り返し、画像の変換はなし、に決める
	void ApplyDefaults( bool bUseDepthBuffer );

	// 描画の終わりに戻す深度バッファの使い方
	FDrawDepthData GetDefaultDepth();
}

//=============================================================================
// FDxLibRenderState3D
//   1 回の Execute の間に変えた、3D の描画状態を覚えて、終わりに元に戻す。
//   変える前の値は、最初に変えるときに DxLib から読む（深度バッファの使い方と画像の貼り方だけは、取得関数が無いので NDxLibRenderState3D の初期値）。
//
//   Apply: 状態の命令（Depth / Culling / TextureAddress / TextureTransform / Fog / HeightFog / Anisotropy）を DxLib へ反映する
//   Restore: 変えた分を、元に戻す
//=============================================================================
class FDxLibRenderState3D
{
public:
	// 3D の描画状態の命令か
	static bool IsStateCommand( EDrawCommand Type );

	// 命令を実行する。この状態の命令でなければ、何もせず false
	bool Apply( const FDrawCommandView& Command );

	// 変えた状態を、元に戻す
	void Restore();

private:
	// フォグ 1 組（距離のフォグか、高さのフォグ）の、変える前の値
	struct FFogBackup
	{
		bool bSaved = false;
		int Enable = 0;
		int Mode = 0;
		int Red = 0;
		int Green = 0;
		int Blue = 0;
		float Start = 0.0f;
		float End = 0.0f;
		float DensityStart = 0.0f;
		float Density = 0.0f;
	};

	bool bDepthChanged = false;
	bool bTextureAddressChanged = false;
	bool bTextureTransformChanged = false;
	bool bCullingChanged = false;
	int OldCulling = 0;
	bool bAnisotropyChanged = false;
	int OldAnisotropy = 1;
	FFogBackup DistanceFog;
	FFogBackup VerticalFog;
};
