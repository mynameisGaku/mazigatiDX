// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContextTargets.h"

// 3Dのカメラと、3Dの描画状態（深度・カリング・画像の貼り方・フォグ・異方性）
class FDrawContextCameraState : public FDrawContextTargets
{
public:
	explicit FDrawContextCameraState( FDrawBuffer& InBuffer ) : FDrawContextTargets( InBuffer ) {}

	//-------------------------------------------------------------------------
	// カメラ（3D の見る位置・向き・レンズ）
	//-------------------------------------------------------------------------
	// 以降の 3D の描画を、Camera で見る（カメラを替える。実行のときに、描画先の大きさで、光軸が通る画面の点を決める）。PopCamera か FCameraScope で、1 つ前のカメラへ戻す
	// （入れ子にできる）。そのオブジェクトの分が終わると、PopCamera が足りなくても、始める前のカメラに戻る。
	// Camera が使えない値（FCamera::IsValid() が false）のときは、何も記録せず false を返す（対の PopCamera も呼ばないこと）
	bool PushCamera( const FCamera& Camera );

	// 1 つ前のカメラへ戻す。PushCamera の数より多く呼んでも、何も起きない
	void PopCamera();

	//-------------------------------------------------------------------------
	// 3D の描画状態（深度バッファ・カリング・画像の貼り方・フォグ・異方性）
	//   どれも、そのオブジェクトの分が終わると、始める前の状態に戻る。値が使えないときは、何も記録しない
	//-------------------------------------------------------------------------
	// 深度バッファの使い方（SetUseZBuffer3D / SetWriteZBuffer3D / SetZBufferCmpType3D / SetZBias3D）。半透明の物を描くときは FDepthState::ReadOnly()。
	// Compare が範囲の外なら、何も記録しない。Bias は ±FDepthState::MaxBias に収める
	void Depth( const FDepthState& State );

	// ポリゴンの裏表の描き分け（SetUseBackCulling）。表 = 画面で頂点が時計回りに見える面。Mode が範囲の外なら、何も記録しない
	void Culling( EDrawCulling Mode );

	// 面の画像の外側（UV が 0..1 の外）の貼り方（SetTextureAddressModeUV）。横 U と縦 V を別々に決められる。範囲の外の値は、何も記録しない
	void TextureAddress( EDrawTextureAddress U, EDrawTextureAddress V );

	// 面の画像の外側の貼り方を、横も縦も同じにする
	void TextureAddress( EDrawTextureAddress Mode );

	// 面に貼る画像の、貼る位置・拡大・回転（SetTextureAddressTransform）。何も変えない変換（IsIdentity）なら、変換をやめる。使えない値（NaN・無限大）なら、何も記録しない
	void TextureTransform( const FTextureTransform& Transform );

	// 面に貼る画像の変換を、元（変換しない）に戻す（ResetTextureAddressTransform）
	void ResetTextureTransform();

	// 距離によるフォグ（SetFogEnable / SetFogMode / SetFogColor / SetFogStartEnd / SetFogDensity）。Mode が None なら、フォグをやめる。使えない値（FFogSettings::IsValid() が false）なら、何も記録しない
	void Fog( const FFogSettings& Settings );

	// 高さによるフォグ（SetVerticalFogEnable / SetVerticalFogMode / SetVerticalFogColor / SetVerticalFogStartEnd / SetVerticalFogDensity）。Mode が None なら、フォグをやめる。
	// 使えない値（FFogSettings::IsValid() が false）なら、何も記録しない
	void HeightFog( const FFogSettings& Settings );

	// 異方性フィルタリングの強さ（SetMaxAnisotropy）。1..16 に収める。Filter( EDrawFilter::Anisotropic ) のときだけ効く（斜めから見た面の画像が、遠くでもぼやけにくくなる）
	void Anisotropy( int32 MaxLevel );

	// いまの描画先の深度バッファを、全体消す（ClearDrawScreenZBuffer）。ここまでに記録した 3D の描画は、消す前の深度で描き終わる。
	// 手前に描く物（一人称の武器など）を、奥の物に隠されないように、あとから描くときに使う。クリップ範囲や描画範囲に関係なく、全体を消す
	void ClearDepth();
};
