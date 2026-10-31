// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// NDxLibLighting
//   フレームワークが DxLib の初期化で決める、ライティングの初期値。
//   DxLib には、光の計算をするか・全体の環境光・材質・光の計算の仕方の取得関数が無いので、描画（Execute）の終わりに戻す先を、ここで持つ。
//   初期値は、DxLib の既定と同じ（実測）なので、何も記録しなければ、DxLib をそのまま使ったときと同じ見た目になる。
//=============================================================================
namespace NDxLibLighting
{
	// 光の計算をする・全体の環境光は 0・材質と光の計算の仕方は DxLib の既定、に決める（DxLib の初期化のあとに 1 度呼ぶ。以降、描画の終わりに戻す先になる）
	void ApplyDefaults();

	// 描画の終わりに戻す材質
	FDrawSurfaceMaterialData GetDefaultMaterial();

	// 描画の終わりに戻す光の計算の仕方
	FDrawLightingOptionsData GetDefaultOptions();
}

//=============================================================================
// FDxLibLighting
//   1 回の Execute の間に変えた、ライティングを覚えて、終わりに元に戻す。
//   変える前の値は、取得関数があるもの（DxLib に最初からあるライトの入り切り）は DxLib から読み、無いものは NDxLibLighting の初期値。
//   PushLight で足したライトは、足したときに DxLib のライトのハンドルを作り、外すとき（か、終わり）に消す。
//
//   Apply: ライティングの命令（UseLighting / DefaultLight / AmbientLight / SurfaceMaterial / LightingOptions / PushLight / PopLight）を DxLib へ反映する
//   Restore: 変えた分を、元に戻す
//=============================================================================
class FDxLibLighting
{
public:
	// ライティングの命令か
	static bool IsStateCommand( EDrawCommand Type );

	// 命令を実行する。このライティングの命令でなければ、何もせず false
	bool Apply( const FDrawCommandView& Command );

	// 変えた状態を、元に戻す（足したライトも、全部外す）
	void Restore();

private:
	bool bUseLightingChanged = false;
	bool bDefaultLightChanged = false;
	int OldDefaultLightEnable = 1;
	// DxLib に最初からあるライトの、影を受けるスロットを変えたか（DxLib の既定は全部。取得関数が無いので、終わりに全部へ戻す）
	bool bDefaultLightSlotsChanged = false;
	bool bAmbientLightChanged = false;
	bool bMaterialChanged = false;
	bool bOptionsChanged = false;

	// PushLight で足したライトのハンドル。-1 は、上限を超えて作らなかった分（PopLight の数を合わせるために積む）
	TArray<int> Lights;
};
