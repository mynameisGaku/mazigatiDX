// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContextCameraState.h"

// ライティング（光）と、シャドウマップ（影の絵）
class FDrawContextLighting : public FDrawContextCameraState
{
public:
	explicit FDrawContextLighting( FDrawBuffer& InBuffer ) : FDrawContextCameraState( InBuffer ) {}

	//-------------------------------------------------------------------------
	// ライティング（光）。値は Lighting.h を参照。どれも、このオブジェクトの分が終わると、始める前の状態に戻る
	//-------------------------------------------------------------------------
	// 光の計算をするか（SetUseLighting）。false だと、ライト・環境光・自己発光の計算をやめて、拡散色と鏡面色を足した色がそのまま出る（空・画面の飾り・光らせたくない物に）。
	// 拡散色と鏡面色は、図形に付けた色（材質が図形の色を使わない設定（bUseVertexDiffuse / bUseVertexSpecular が false）なら、材質の色）。図形の鏡面色が黒（既定）なら、図形の色のまま。何も記録しなければ true
	void UseLighting( bool bEnabled );

	// DxLib に最初からある 1 つのライト（白い方向ライト。向きは (1, -1, 1)）の入り切り（SetLightEnable）。自分のライトだけで照らしたいときは、false にする。
	// ライトは、これも含めて、同時に FLight::MaxActiveLights（6）個まで。
	// ShadowMapSlots は、このライトが影を受けるスロットのビットの和（NDrawShadowMap::SlotBit）。既定は全部（0〜2 の外のビットは無視する）
	void DefaultLight( bool bEnabled, int32 ShadowMapSlots = NDrawShadowMap::AllSlots );

	// 全体の環境光の色（SetGlobalAmbientLight）。すべての面を、向きに関係なく、この色で底上げする。面の材質の Ambient が 0（既定）だと効かない。
	// 値が NaN・無限大のときは、何も記録しない
	void AmbientLight( const FColor& Color );

	// 以降に描く図形の材質（SetMaterialParam / SetMaterialUseVertDifColor / SetMaterialUseVertSpcColor）。値が使えない（FSurfaceMaterial::IsValid() が false）ときは、何も記録しない
	void SurfaceMaterial( const FSurfaceMaterial& Material );

	// 光の計算の仕方（SetUseSpecular / SetUsePixelLighting / SetUseHalfLambertLighting / SetUseLightAngleAttenuation）
	void LightingOptions( const FLightingOptions& Options );

	// ライトを 1 つ足す（CreateDirLightHandle / CreatePointLightHandle / CreateSpotLightHandle）。以降の図形が、このライトでも照らされる。
	// 入れ子にできる。PopLight か FLightScope で外す。そのオブジェクトの分が終わると、PopLight が足りなくても、足した分は全部外れる。
	// ライトは合計 FLight::MaxActiveLights（6）個まで（DefaultLight が有効なら、それも数える）。超えた分は、足しても効かない（PopLight の数は合わせること）。
	// 値が使えない（FLight::IsValid() が false）ときは、何も記録せず false を返す（対の PopLight も呼ばないこと）
	bool PushLight( const FLight& Light );

	// いちばん新しく足したライトを外す。PushLight の数より多く呼んでも、何も起きない
	void PopLight();

	//-------------------------------------------------------------------------
	// シャドウマップ（影の絵）。値は ShadowMapView.h、シャドウマップは Framework/Asset/ShadowMap.h を参照。
	// 影は、2 段で作る: ① BeginShadowMap〜EndShadowMap の間に、影を落とすものを描く（画面ではなく、シャドウマップへ描かれる）。
	// ② UseShadowMap のあとに、影を受けるものを描く（光を受ける図形・ライトの照らし方は、ほかの 3D 図形と同じ）。光の計算を切る（UseLighting( false )）と、影は出ない。
	// どれも、このオブジェクトの分が終わると、始める前の状態に戻る（始めたままの影の絵は終わらせ、使っているスロットは外す）。
	// シャドウマップは、TSharedPtr・ポインター・参照・ハンドル（FShadowMapHandle）のどれでも渡せる。記録してから描かれるまで、手放さないこと
	//-------------------------------------------------------------------------
	// シャドウマップ ShadowMap への描画を始める（ShadowMap_DrawSetup）。以降の 3D の図形・モデルは、画面でなく、View の光から見た深度としてシャドウマップへ描かれる。
	// 始めるたびに、中身は消えて、描き直される。入れ子にできない（始めている間の BeginShadowMap は無視する。対の EndShadowMap も無視する）。
	// 影の絵の中で効くのは、3D の図形・モデルと、その描き方の状態（ワールド変換・カリング・ブレンド・アルファテスト・画像の貼り方）。
	// 描画先・カメラ・クリップ・Clear / ClearDepth / CopyScreen / UseShadowMap の命令は、影の絵を壊すので、実行のときに無視する。
	// 影を落とすのは、ほぼ不透明に描かれたもの: 不透明度（ブレンド Alpha の値・頂点のアルファ・画像のアルファ）が低いものは落とさない。境目は 255 のうち、
	// 頂点（Primitive3D）のアルファとブレンドの値は 149、箱（Cube3D）のブレンドの値は 189、球（Sphere3D）は 195（実測。これ以上が影を落とす）。加算ブレンドも落とさない。
	// 画像のアルファが低い画素も落とさない（葉の切り抜き）。
	// カリングは、画面と同じ設定が効く（裏面を描かない設定のとき、裏面は影を落とさない）。
	// ShadowMap か View が使えない（無効なハンドル・View.IsValid() が false）ときは、false を返す。このときも、命令は記録する: 対の EndShadowMap まで、図形を描かずに飛ばす
	// （飛ばさないと、影を落とすはずの図形が、そのまま画面に描かれてしまう）。シャドウマップの中身は、描き直されずに、前のまま残る。対の EndShadowMap は、必ず呼ぶこと
	bool BeginShadowMap( FShadowMapHandle ShadowMap, const FShadowMapView& View );

	template<typename TShadowMap, typename = TEnableIfT<!bIsSame<TShadowMap, FShadowMapHandle>>>
	bool BeginShadowMap( const TShadowMap& ShadowMap, const FShadowMapView& View )
	{
		return BeginShadowMap( ToShadowMapHandle( ShadowMap ), View );
	}

	// シャドウマップへの描画を終えて、元の描画先・カメラに戻す（ShadowMap_DrawEnd）。BeginShadowMap の数より多く呼んでも、何も起きない
	void EndShadowMap();

	// スロット Slot（0〜2）に、シャドウマップを使わせる（SetUseShadowMap）。以降に描く光を受ける図形に、影ができる。
	// 同じスロットに別のシャドウマップを指定すると、入れ替わる。スロットが 0〜2 の外・シャドウマップが無効なときは、何も記録せず false を返す
	bool UseShadowMap( int32 Slot, FShadowMapHandle ShadowMap );

	template<typename TShadowMap, typename = TEnableIfT<!bIsSame<TShadowMap, FShadowMapHandle>>>
	bool UseShadowMap( int32 Slot, const TShadowMap& ShadowMap )
	{
		return UseShadowMap( Slot, ToShadowMapHandle( ShadowMap ) );
	}

	// スロット Slot の影を、使わなくする。スロットが 0〜2 の外なら、何も記録しない
	void StopUsingShadowMap( int32 Slot );

	// シャドウマップの中身（光から見た深度。近いほど暗い赤）を、画面の矩形 (X0, Y0)〜(X1, Y1) に描く（TestDrawShadowMap。影の範囲や向きを確かめる用）。
	// シャドウマップが無効・矩形が空（X1 <= X0 か Y1 <= Y0）のときは、何も記録せず false を返す
	bool PreviewShadowMap( FShadowMapHandle ShadowMap, int32 X0, int32 Y0, int32 X1, int32 Y1 );

	template<typename TShadowMap, typename = TEnableIfT<!bIsSame<TShadowMap, FShadowMapHandle>>>
	bool PreviewShadowMap( const TShadowMap& ShadowMap, int32 X0, int32 Y0, int32 X1, int32 Y1 )
	{
		return PreviewShadowMap( ToShadowMapHandle( ShadowMap ), X0, Y0, X1, Y1 );
	}
};
