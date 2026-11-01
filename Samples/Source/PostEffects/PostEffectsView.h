// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/PostEffects/PostEffectsDemoState.h"

//=============================================================================
// CPostEffectsView
//   メインの 3D の画面。4 段で描く:
//   ① 場面（夜の広場・光る玉・ネオンの溝）を、16 ビットの浮動小数点の描画先 SceneColor へ描く。ピクセルシェーダーは、
//      色（SV_Target0）と同時に、深度（SV_Target1）を、出力 1 の描画先 SceneDepth へ書く（ShaderOutputTarget。複数の描画先への同時の書き出し）。
//   ② 明るい所（露出を掛けて、境目を超えた分）を、半分の大きさの BloomA へ抜き出す。
//   ③ BloomA を、横（→ BloomB）・縦（→ BloomA）と、広げながら 3 回ずつぼかす。
//   ④ 場面の色 + ブルームに、深度で霧をかけ、トーンマップで 0〜1 に収めて、見せる範囲に貼る（ShaderRect）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CPostEffectsView : public CGameObject
{
public:
	explicit CPostEffectsView( const TSharedPtr<FPostEffectsDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	// ① 場面（地面・物・光る玉）を記録する
	void RecordScene( FDrawContext& CommandList ) const;

	// ②〜③ ブルームを作る
	void RecordBloom( FDrawContext& CommandList ) const;

	// ④ 合成して、見せる範囲に貼る
	void RecordComposite( FDrawContext& CommandList ) const;

	TSharedPtr<FPostEffectsDemoState> State;
};
