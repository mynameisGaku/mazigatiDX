// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CTargetShowcase
//   描画先（レンダーターゲット）の使い方を、動かしながら見せる。
//   - 左: 小さな「ミニマップ」（透明な背景を持つ描画先）に、動く絵を描く（FTargetScope）。ふつうの画像として、画面へ貼る
//   - 中: 同じ描画先を、2 倍に引き伸ばして貼る
//   - 右上: 画面の一部（左の絵）を、別の描画先へ写して（CopyScreen）、貼る
//   - 右下: 左の絵の上を動く印の下の色を、描画先から読む（ReadPixel）。S キーで、描画先を PNG に保存する（Save）
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ（DxLib も実行先も知らない）。読み取りと保存は、描いたあとの内容に対して、Tick で行う。
//=============================================================================
class CTargetShowcase : public CGameObject
{
public:
	// InOrigin は、左の絵の左上（画面の座標。ピクセル）。InPattern は、ミニマップの中へ描く画像。
	// InMinimap は、透明を持つ描画先、InSnapshot は、同じ大きさの描画先（どちらも、このオブジェクトが持つ）
	CTargetShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InPattern, const TSharedPtr<CRenderTarget>& InMinimap, const TSharedPtr<CRenderTarget>& InSnapshot );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 市松模様（透明な部分が分かるよう、下に敷く）
	void DrawChecker( FDrawContext& CommandList, int32 Left, int32 Top, int32 Width, int32 Height ) const;

	// ミニマップの中身（描画先の座標。左上が原点）
	void DrawMinimapScene( FDrawContext& CommandList ) const;

	// 印の位置（ミニマップの中の座標。ピクセル）
	void GetMarker( int32& OutX, int32& OutY ) const;

	// 左の絵の左上（画面の座標。ピクセル）
	FVector2 Origin;

	// ミニマップに描く画像
	TSharedPtr<CTexture> Pattern;

	// 描画先
	TSharedPtr<CRenderTarget> Minimap;
	TSharedPtr<CRenderTarget> Snapshot;

	// 印の下の色（Tick で、ミニマップから読んだもの）。読めていれば true
	FColor32 ProbeColor;
	bool bProbeValid = false;

	// 保存した結果の表示
	FString SaveStatus;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
