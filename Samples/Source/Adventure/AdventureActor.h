// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CAdventureActor
//   Adventure の、コードで作った 3D のオブジェクト（主人公・敵・宝箱・拾うもの・門・焚き火）の基底。
//   3D の絵は、カメラとライトを決めたあとでしか記録できないので、Draw ではなく Record3D に描く。
//   CAdventureView が、カメラとライトを決めてから、すべてのアクターの Record3D を呼ぶ。
//=============================================================================
class CAdventureActor : public CGameObject
{
public:
	virtual void Record3D( FDrawContext& CommandList ) {}

protected:
	// 置き物（親）の、世界での位置。親が無ければ、原点
	FVector3 GetPlaceholderPosition() const;

	// 置き物（定義のオブジェクト）の番号。置き物でなければ 0（セーブで、開けた・倒した・拾った、を覚えるのに使う）
	int32 GetPlaceholderId() const;
};

//=============================================================================
// IAdventureInteractable
//   主人公が、E キーで調べられるもの（宝箱・門・焚き火）。近くにあるとき、案内（Prompt）が、画面に出る。
//=============================================================================
class IAdventureInteractable
{
public:
	virtual ~IAdventureInteractable() = default;

	virtual FVector3 GetInteractPosition() const = 0;
	virtual const char* GetPrompt() const = 0;
	virtual bool CanInteract() const { return true; }
	virtual void Interact() = 0;
};
