// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CFontShowcase
//   フォントの指定（FFontStyle）の違いを、同じ文字列を、行ごとに違うフォントで描いて見せる。
//   アンチエイリアスの段階・斜体と太さ・縁取り（色は、描くときに指定する）・字間・行の送り幅・文字の代わりの画像（アイコン）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。行の高さは、フォントが作るときに測って持っている寸法（GetMetrics）から決める。
//=============================================================================

// 見せる 1 行（見出し・フォント・文字列・縁の色）
struct FFontSampleRow
{
	FString Caption;
	TSharedPtr<CFont> Font;
	FString Text;
	FColor32 EdgeColor = FColor32( 0, 0, 0 );
};

class CFontShowcase : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InRows は、見せる行（このオブジェクトが持つ）
	CFontShowcase( const FVector2& InOrigin, const TArray<FFontSampleRow>& InRows );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	TArray<FFontSampleRow> Rows;
};
