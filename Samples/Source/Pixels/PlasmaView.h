// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/DynamicTexture.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CPlasmaView
//   CPU 側の画像（FPixelImage）に、毎フレーム、画素を書いて、画像（CDynamicTexture）の中身を替える（Update）。
//   模様（プラズマ）は、画素ごとに計算し、その上に、半透明の光（Blit の Alpha）を重ねる。S キーで、PNG に保存して、読み直して、同じ画像か確かめる。
//   Draw( FDrawContext& CommandList ) は、画像を IImage として渡して、何を描くかを記録するだけ（DxLib も実行先も知らない）。
//=============================================================================
class CPlasmaView : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InCanvas は、書き込む CPU 側の画像（このオブジェクトが持つ）。
	// InTexture は、中身を毎フレーム替える画像（InCanvas と同じ大きさ）
	CPlasmaView( const FVector2& InOrigin, const FPixelImage& InCanvas, const TSharedPtr<CDynamicTexture>& InTexture );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	// CPU 側の画像（毎フレーム、ここへ書いて、Texture へ反映する）と、重ねる光（半透明の円）
	FPixelImage Canvas;
	FPixelImage Glow;

	// GPU 側の画像
	TSharedPtr<CDynamicTexture> Texture;

	// 印の下の色（Canvas から読んだもの）
	FColor32 ProbeColor;

	// 保存した結果の表示
	FString SaveStatus;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
