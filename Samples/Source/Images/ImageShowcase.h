// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CImageShowcase
//   2D の画像の描画 9 種類のうち 7 つ（Image / ImageStretch / ImageQuad / ImagePart / SpritePivot / Tile / CircleGauge）を、
//   1 枚の画像（TSharedPtr<CTexture>）で、横に並べて、動かしながら見せる。名前は、それぞれの下。
//   Draw( FDrawContext& CommandList ) は、画像を IImage として渡して、何を描くかを記録するだけ（DxLib も実行先も知らない）。
//=============================================================================
class CImageShowcase : public CGameObject
{
public:
	// InOrigin は、左端の絵の中心（画面の座標。ピクセル）。絵は、そこから右へ並ぶ。InTexture は、描く画像（このオブジェクトが持つ）
	CImageShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InTexture );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 絵 1 つぶんの描き方（Center は、その絵の中心）
	void DrawImage( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawStretch( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawQuad( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawPart( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawPivot( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawTile( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawGauge( FDrawContext& CommandList, const FVector2& Center ) const;

	// 左端の絵の中心（画面の座標。ピクセル）
	FVector2 Origin;

	// 描く画像
	TSharedPtr<CTexture> Texture;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
