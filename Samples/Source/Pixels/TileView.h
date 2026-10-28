// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CTileView
//   1 枚のスプライトシートを、格子で、1 コマずつの画像に分けたもの（CAssetManager::SplitTexture）を、並べて見せる。
//   上の段: 全コマを、番号とともに並べる。下: コマを順番に切り替えて、アニメーションのように見せる。
//   分けた画像は、元の画像を共有する派生画像なので、メモリは増えない。Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CTileView : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InPieces は、SplitTexture で分けたコマ（このオブジェクトが持つ）
	CTileView( const FVector2& InOrigin, const TArray<TSharedPtr<CTexture>>& InPieces );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	// 分けたコマ
	TArray<TSharedPtr<CTexture>> Pieces;

	// 動きの時間（秒）
	float32 Time = 0.0f;
};
