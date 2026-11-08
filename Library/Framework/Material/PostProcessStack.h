// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Material/Material.h"

class CAssetSubsystem;

//=============================================================================
// CPostProcessStack: ポストプロセスのマテリアルを並べた、チェーン
//   PostProcess ドメインのマテリアルを、順番に、シーンの絵へ掛ける。順番・有効 / 無効・強さを持つ。
//   適用先は、描画先（PushTarget）か画面か、UI の RenderTarget の部品の中身など、どこにでも（Record で描く矩形を決める）。
//
//   CPostProcessStack Stack;
//   Stack.Add( Vignette );
//   Stack.Add( ColorGrade, 0.6f );                                       // 強さ 0.6（マテリアルに Intensity という数のパラメーターがあれば、その値になる）
//   // 毎フレーム（Scene は、場面を描いた描画先。Depth は、深度を書いた描画先で、使わなければ null）
//   if ( !Stack.Record( Draw, Assets, Scene, Depth, 640, 360, 0.0f, 0.0f, 640.0f, 360.0f, FrameState ) )
//   {
//       // 有効なパスが無い（何もしなかった）: 呼ぶ側が、Scene をそのまま描く
//   }
//
//   - 有効なパスが 1 つなら、シーンを読んで、そのまま矩形へ描く。2 つ以上なら、途中の結果を、スタックが持つ、2 枚の作業用の描画先（Scene と同じ大きさ）に、交互に描き、
//     最後のパスだけを、矩形へ描く。Scene の大きさが変わったら、作業用の描画先を作り直す。
//   - 強さ: Intensity の数のパラメーターを持つマテリアルは、その値を、強さにする（持たなければ、強さは、見た目に使われない）。
//   - 描いたあとの描画の状態は、CMaterial::DrawPostProcess と同じく、元に戻さない。
//=============================================================================
class CPostProcessStack
{
public:
	struct FEntry
	{
		TSharedPtr<CMaterial> Material;
		bool bEnabled = true;
		float32 Intensity = 1.0f;
	};

	// 末尾に足す。ドメインが PostProcess でない・null なら、足さず -1。足した番号を返す
	int32 Add( const TSharedPtr<CMaterial>& Material, float32 Intensity = 1.0f );
	bool Remove( int32 Index );
	// From の項目を、To の位置へ動かす（範囲の外は false）
	bool Move( int32 From, int32 To );
	void Clear() { Entries.Empty(); }

	int32 Num() const { return Entries.Num(); }
	FEntry& GetEntry( int32 Index ) { return Entries[Index]; }
	const FEntry& GetEntry( int32 Index ) const { return Entries[Index]; }
	// 有効で、使えるマテリアルの数
	int32 NumEnabled() const;

	// チェーンを記録する。描けたら true。有効なパスが無いか、シーンが無いか、作業用の描画先を作れなければ false（何も記録しない）
	bool Record( FDrawContext& Draw, CAssetSubsystem& Assets, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth, int32 SceneWidth, int32 SceneHeight, float32 X, float32 Y, float32 Width,
				 float32 Height, const FMaterialFrameState& State );

private:
	bool EnsureTargets( CAssetSubsystem& Assets, int32 Width, int32 Height );

	TArray<FEntry> Entries;
	TSharedPtr<CRenderTarget> Targets[2];
	int32 TargetWidth = 0;
	int32 TargetHeight = 0;
};
