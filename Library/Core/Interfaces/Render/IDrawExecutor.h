// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/DrawSpan.h"
#include "Library/Core/Render/OffscreenCaptureDesc.h"
#include "Library/Math/Color.h"

//=============================================================================
// IDrawExecutor
//   描画コマンドを、実際に描画する。メインスレッドだけから呼ばれる。
//
//   Execute: Span のコマンドを、先頭から順に実行する。
//     - Span の中で変えた状態（ブレンドモード・明るさ・WorldMatrix・加算する色・アルファテスト・描画モード・クリップ範囲・2D の変換・
//       深度バッファの使い方・カリング・テクスチャの貼り方・フォグ・異方性）は、Span の終わりで元に戻る（次のオブジェクトに漏れない）。
//       クリップ範囲が空のあいだは、描く命令だけを飛ばす
//     - ライト・フォントなどは触らない（呼び出し側が決めたものをそのまま使う）。描画先・カメラは、命令で替えた分だけ、Span の終わりで戻す
//   実装: CDxLibDrawExecutorSubsystem（DxLib）。確認用に、記録するだけの実装を作ってもよい。
//=============================================================================
class IDrawExecutor
{
public:
	virtual ~IDrawExecutor() = default;

	virtual void Execute( const FDrawSpan& Span ) = 0;
	// 実行先がネイティブ材質シェーダーを安全に扱えるかを返す。未対応の実行先は従来どおり false。
	virtual bool SupportsEffectMaterials() const { return false; }
	// 実行先が材質付きの帯を描画できるかを返す。未対応の実行先は従来どおり false。
	virtual bool SupportsEffectMaterialRibbons() const { return false; }
	// 実行先がノイズ画像によるUVゆがみを描画できるかを返す。未対応の実行先は false。
	virtual bool SupportsEffectUVDistortion() const { return false; }
	// 実行先がノイズ画像による材質帯のUVゆがみを描画できるかを返す。未対応の実行先は false。
	virtual bool SupportsEffectRibbonUVDistortion() const { return false; }
	// 実行先が材質付き頂点メッシュを描画できるかを返す。未対応の実行先は false。
	virtual bool SupportsEffectMaterialMesh() const { return false; }
	// ModelMaterial3Dでモデル内の選択材質へ効果を描けるか返す。
	virtual bool SupportsModelMaterialEffects() const { return false; }
	// 通常model描画1回だけのframe pose一時適用に対応するか返す。
	virtual bool SupportsModelFrameOverrides() const { return false; }

	// 撮影のための描き直しを始める: 以後の Execute を、Desc の大きさの別の描画先へ、2D を Desc.Scale 倍にして描く。
	// 対応しない実行先・作れない大きさ・すでに始めているときは false（何も変えない）。EndOffscreenCapture と必ず対にする
	virtual bool BeginOffscreenCapture( const FOffscreenCaptureDesc& /*Desc*/ ) { return false; }
	// 描き直しを終え、描いた画素（Width x Height、左上から行の順。a は描画先の透明度）を Out へ写して、元の描画先に戻す。始めていなければ false
	virtual bool EndOffscreenCapture( TArray<FColor32>& /*Out*/ ) { return false; }
};
