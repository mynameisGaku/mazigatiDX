// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class CPcmStream;

//=============================================================================
// CSynthPad
//   四角い領域の中を、左ボタンを押しながらなぞると、その場で計算した音が鳴る（横 = 高さ、縦 = 大きさ）。
//   音のファイルを使わず、計算した波形を、CPcmStream に、次々に足していく（シンセサイザーの作り方）。
//   足りなくなると無音になるので、残りが少なくなったら足す。ボタンを離したら、足した分を捨てて止める。
//=============================================================================
class CSynthPad : public CGameObject
{
public:
	// InMin / InMax は領域の左上と右下（画面の座標）
	CSynthPad( const FVector2& InMin, const FVector2& InMax );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	// いまの周波数と音量で、時間 Seconds ぶんの波形を、ストリームに足す（位相は、前の続きから）
	void Feed( float32 Seconds );

	FVector2 Min;
	FVector2 Max;
	TSharedPtr<CPcmStream> Stream;
	bool bActive = false;
	float32 Frequency = 440.0f;
	float32 Volume = 0.0f;

	// サイン波の位相（0..1。つなぎ目で途切れないように、足すたびに続きから）
	float32 Phase = 0.0f;
};
