// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/State/Lighting.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// CVertexLighting
//   共通の描画命令のライティング（光）を、頂点ごとに CPU で計算する。GPU や描画先を知らない。
//   DxLib の固定の光の計算と同じ式（Lighting.h の説明と同じ）で、命令の互換の経路（頂点色で描く基本 3D）が使う。
//
//     色 ＝ 自己発光 ＋ 全体の環境光 x 材質の環境色
//          ＋ ライトごとの（距離と円錐の減衰 x ( 拡散色 x ライトの拡散色 x 面の向き ＋ 材質の環境色 x ライトの環境色 ＋ 鏡面色 x ライトの鏡面色 x ハイライト )）
//
//   - 同時に効くライトは FLight::MaxActiveLights（6）まで。最初からある 1 つのライト（白い方向ライト。向き (1, -1, 1)）が有効なら、それが先。
//     超えて足したライトは効かないが、外すときの数は合う（Pop は、効かなかった分も 1 つずつ戻す）。
//   - 光の計算を切ると、拡散色 ＋ 鏡面色がそのまま出る。
//   - 画素ごとの光（FLightingOptions::bPixelLighting）は、この計算では頂点ごとになる。
//=============================================================================
class CVertexLighting
{
public:
	// 光の計算をするか（既定は、する）
	void SetEnabled( bool bInEnabled ) { bEnabled = bInEnabled; }
	bool IsEnabled() const { return bEnabled; }
	// 最初からある 1 つのライトの入り切りと、そのライトが影を受けるスロットのビットの和
	void SetDefaultLight( bool bInEnabled, int32 InShadowSlots = 7 )
	{
		bDefaultLight = bInEnabled;
		DefaultLightShadowSlots = InShadowSlots;
	}
	// 全体の環境光
	void SetAmbient( const FColor& Color ) { Ambient = Color; }
	// 面の材質。使えない値なら false（変えない）
	bool SetMaterial( const FSurfaceMaterial& InMaterial );
	const FSurfaceMaterial& GetMaterial() const { return Material; }
	// 光の計算の仕方
	void SetOptions( const FLightingOptions& InOptions ) { Options = InOptions; }
	const FLightingOptions& GetOptions() const { return Options; }
	// 全体の環境光
	const FColor& GetAmbient() const { return Ambient; }
	// いま効くライトを、効く順（最初からあるライトが先）に、OutLights（FLight::MaxActiveLights 個分）へ入れて、その数を返す。画素ごとの光（GPU）へ渡すときに使う
	int32 CollectActiveLights( FLight ( &OutLights )[FLight::MaxActiveLights] ) const;
	// ライトを 1 つ足す。使えない値なら false（足さない）
	bool PushLight( const FLight& Light );
	// いちばん新しく足したライトを外す。無ければ何もしない
	void PopLight();

	// 世界の点 Point（単位の法線 Normal）の色（0..1 を超えることがある）。Eye はカメラの位置。
	// VertexDiffuse / VertexSpecular は、図形に付けた色（材質の設定で、使うかどうかが決まる）
	// ShadowMask が 0 でないとき、ライトが影を受けるスロット（FLight::ShadowMapSlots）と重なるライトの分は、戻り値でなく OutShadowed へ足す（影の中で消える分）。
	FColor Evaluate( const FVector3& Point, const FVector3& Normal, const FVector3& Eye, const FColor& VertexDiffuse, const FColor& VertexSpecular, int32 ShadowMask = 0, FColor* OutShadowed = nullptr ) const;

private:
	FSurfaceMaterial Material;
	FLightingOptions Options;
	FColor Ambient = FColor( 0.0f, 0.0f, 0.0f, 0.0f );
	TArray<FLight> Lights;   // 足した順
	bool bEnabled = true;
	bool bDefaultLight = true;
	int32 DefaultLightShadowSlots = 7;   // 最初からあるライトが、影を受けるスロット
};
