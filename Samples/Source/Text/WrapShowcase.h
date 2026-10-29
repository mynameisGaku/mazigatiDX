// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Text/TextLayout.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CWrapShowcase
//   文字列を、箱の幅に収まるように折り返して描く（CFont::Layout と CFont::RecordLayout）。
//   上の箱は、幅が動き、そのたびに折り返しが変わる（日本語は 1 文字ごとに、英語は単語の区切りで折れる。句読点は行頭に来ない）。
//   下の箱は、幅が決まっていて、右揃えにして、3 行までで切る（切れたら印を出す）。
//   文字を測る（Layout）のは、メインスレッドの Tick で行い、Draw の中では、できた結果を記録するだけ（RecordLayout）。
//=============================================================================
class CWrapShowcase : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InFont は本文のフォント、InText は折り返す文章
	CWrapShowcase( const FVector2& InOrigin, const TSharedPtr<CFont>& InFont, const FString& InText );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 動く箱の幅と、折り返した結果を作る（メインスレッドで呼ぶ）
	void UpdateMovingLayout();

	// 箱の高さ（行数 x 行の送り幅。ピクセル）
	int32 GetBoxHeight( const FTextLayout& Layout ) const;

	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	TSharedPtr<CFont> Font;
	FString Text;

	// 動く箱の幅と、折り返した結果
	int32 MovingWidth = 0;
	FTextLayout Moving;

	// 決まった幅の箱の折り返した結果（右揃え、3 行まで）
	FTextLayout Fixed;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
