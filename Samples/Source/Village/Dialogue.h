// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Text/TextLayout.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CDialogue
//   画面の下の会話の窓。文を、窓の幅で折り返して（CFont::Layout）、1 文字ずつ出す。Enter（か Space）で、全部を出す → 次の文 → 閉じる。
//   最初に、はじまりの説明を出す。あとは、村人が話しかけたとき（FVillageState::bTalkRequested）に、村人の話を出す。
//   折り返しは、文が変わるときに 1 度だけ計算し（文字を測るので、Tick で行う）、Draw は、できた結果を記録するだけ。
//=============================================================================
class CDialogue : public CGameObject
{
public:
	// InBodyFont は本文のフォント、InNameFont は話す人の名前のフォント
	CDialogue( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CFont>& InBodyFont, const TSharedPtr<CFont>& InNameFont );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// はじまりの説明と、村人の話を開く
	void OpenIntroduction();
	void OpenVillagerTalk();

	// 文の組を開く（Speaker は話す人の名前）。最初の文から出す
	void Open( const FString& InSpeaker, const TArray<FString>& InPages );

	// いまの文を折り返して、出し始める
	void StartPage();

	void Close();

	TSharedPtr<FVillageState> State;
	TSharedPtr<CFont> BodyFont;
	TSharedPtr<CFont> NameFont;

	FString Speaker;
	TArray<FString> Pages;
	int32 PageIndex = 0;

	// いまの文の折り返しの結果と、その文字数（行ごとの文字数の合計）、ここまでに出した文字数
	FTextLayout Layout;
	int32 TotalChars = 0;
	float32 RevealedChars = 0.0f;

	bool bOpen = false;
};
