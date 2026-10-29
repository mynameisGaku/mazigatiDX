// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CVillager
//   道のそばに立っている村人。主人公が近づくと、頭の上に「!」を出す。そこで Enter（か Space）を押すと、話しかける
//   （FVillageState::bTalkRequested を立てる。会話の窓 CDialogue が受け取る）。
//   Draw( FDrawContext& CommandList ) は、足元の影と、少し揺れる姿と、「!」を記録するだけ。
//=============================================================================
class CVillager : public CGameObject
{
public:
	// InPosition は足元の世界の座標。InSheet はキャラクターの画像、InFont は「!」の文字のフォント
	CVillager( const TSharedPtr<FVillageState>& InState, const FVector2& InPosition, const TSharedPtr<CTexture>& InSheet, const TSharedPtr<CFont>& InFont );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FVillageState> State;
	FVector2 Position;
	TSharedPtr<CTexture> Sheet;
	TSharedPtr<CFont> Font;

	// 動きの時間（秒）と、主人公が話しかけられる近さにいるか
	float32 Time = 0.0f;
	bool bNear = false;
};
