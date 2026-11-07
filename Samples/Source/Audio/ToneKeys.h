// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class CSound;

//=============================================================================
// CToneKeys
//   8 つの鍵盤（ドレミファソラシド）。数字キー 1〜8、または、鍵盤をクリックで鳴る。
//   鳴らす音は 1 つだけで、高さは、Pitch（周波数の倍率）で変える。位置が左右に離れているので、パン（左右の振り分け）も、鍵盤ごとに変えている。
//   CAudioSubsystem::Play( 音, FSoundPlayParams ) の、Volume / Pitch / Pan の見本。
//=============================================================================
class CToneKeys : public CGameObject
{
public:
	// InOrigin は、鍵盤の並びの左上（画面の座標）
	explicit CToneKeys( const FVector2& InOrigin );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	static constexpr int32 NumKeys = 8;

private:
	void Press( int32 Index );

	FVector2 Origin;
	TSharedPtr<CSound> Note;

	// 鍵盤ごとの、押してからの時間（光らせるため）。押していなければ大きい値
	float32 SinceTouched[NumKeys];
};
