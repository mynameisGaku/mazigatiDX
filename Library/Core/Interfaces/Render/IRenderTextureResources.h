// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Core/Render/ComputeTextureCapabilities.h"
#include "Library/Core/Render/RenderTargetCapabilities.h"
#include "Library/Core/Render/TextureDrawView.h"
#include "Library/Math/Color.h"

// 画像のGPU資源を作り、所有する窓口。ファイル・codec・ImGuiを知らない。
// ゲームスレッドから呼ぶ。アセットの登録と、この資源の登録を混同しない。
class IRenderTextureResources
{
public:
	// 窓口経由の破棄を許可する。所有者がGPUの完了まで生存させる。
	virtual ~IRenderTextureResources() = default;
	// 左上から行順の非乗算済みRGBA8を呼び出し中に複写する。
	// 不正な寸法・画素・作成失敗は無効値。既存資源へ影響しない。
	virtual FTextureResourceHandle CreateTextureRGBA8( int32 Width, int32 Height, const FColor32* Pixels ) = 0;
	// 実行先のcompute画像の能力を返す。未対応・照会失敗ではOutを保ってfalse。
	virtual bool GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const { return false; }
	// 初期RGBA8を所有する読み書き可能なcompute画像を作る。描画でも使える。未対応・不正入力では無効値。
	virtual FTextureResourceHandle CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) { return {}; }
	// 実行先の描画先画像と読み取れる深度の能力を返す。未対応・照会失敗ではOutを保ってfalse。
	virtual bool GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const { return false; }
	// 初期色で塗ったRGBA8の描画先画像を作る。通常描画やcomputeのRead入力でも使える。未対応・不正入力では無効値。
	virtual FTextureResourceHandle CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor = FColor32( 0, 0, 0, 255 ) ) { return {}; }
	// 初期深度（0.0〜1.0）で塗った読み取れるD32深度を作る。描画パスの深度とcomputeのR32Float Read入力で使える。
	virtual FTextureResourceHandle CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth = 1.0f ) { return {}; }
	// 同じ寸法で内容を更新する。失敗時は内容と借用viewを保つ。
	// 更新前に取得したviewは、それを使ったGPU処理が完了するまで保つ。
	virtual bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32* Pixels ) = 0;
	// 番号を直ちに無効にする。借用viewの資源はGPU完了後に解放する。
	// 無効・解放済みの番号は無視し、別の資源へ作用しない。
	virtual void ReleaseTexture( FTextureResourceHandle Texture ) = 0;
	// 描画中だけ使うviewを貸す。失敗時はOutを保つ。
	// 更新・解放前のviewも記録済みの描画で使える。APIを跨いで共有しない。
	virtual bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView& Out ) const = 0;
};
