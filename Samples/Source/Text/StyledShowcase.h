// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CStyledShowcase
//   文字列の飾り（FTextStyle）を、動かしながら見せる: 拡大（脈打つ）・回転（基準点を中心に回る）・影・縦書き・基準点の位置。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ（CFont::RecordStyled。文字は測らない）。
//=============================================================================
class CStyledShowcase : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InFont は、飾りをつける文字のフォント
	CStyledShowcase( const FVector2& InOrigin, const TSharedPtr<CFont>& InFont );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	TSharedPtr<CFont> Font;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
