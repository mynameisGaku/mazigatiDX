// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Math.h"

//=============================================================================
// FVillageState
//   サンプル Village の、オブジェクトどうしで共有する状態（主人公の位置・カメラ・集めた金貨・昼夜・会話の中か）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。オブジェクトは、自分の分だけを書き、ほかは読む。
//=============================================================================
struct FVillageState
{
	// 世界を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 主人公の足元の、世界の座標
	FVector2 PlayerPosition;

	// 見せる範囲の左上に来る、世界の座標（整数にそろえる。タイルの継ぎ目がずれないようにするため）
	FVector2 CameraPosition;

	// 金貨の世界の座標と、集めたか（同じ番号が対応する）
	TArray<FVector2> CoinPositions;
	TArray<bool> CoinCollected;
	int32 Coins = 0;

	// 走るための体力（0〜1）
	float32 Stamina = 1.0f;

	// 夜の暗さ（0 は昼、1 は真夜中）と、向かっている先（夜なら true）
	float32 Darkness = 1.0f;
	bool bNight = true;

	// 会話の窓が開いているか。村人に話しかけたい（村人が true にして、会話の窓が受け取る）
	bool bDialogueOpen = false;
	bool bTalkRequested = false;
	// 会話の窓を閉じた時刻（Time）。閉じたキーで、すぐに話しかけ直さないようにする
	float32 DialogueClosedTime = -10.0f;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// 世界の座標を、画面の座標にする変換（移動だけ）
	FMatrix4x4 GetWorldMatrix() const
	{
		return FMatrix4x4::Translate( FVector3( static_cast<float32>( ViewLeft ) - CameraPosition.x, static_cast<float32>( ViewTop ) - CameraPosition.y, 0.0f ) );
	}

	// 世界の座標を、画面の座標にする
	FVector2 WorldToScreen( const FVector2& World ) const
	{
		return FVector2( World.x - CameraPosition.x + static_cast<float32>( ViewLeft ), World.y - CameraPosition.y + static_cast<float32>( ViewTop ) );
	}
};

//=============================================================================
// FWorldScope
//   世界の座標で描くための範囲。見せる範囲にクリップして、2D の変換にカメラを掛ける。スコープを抜けると、変換を戻す。
//   世界の中のオブジェクトは、Draw の最初に、これを置く（クリップと変換は、Draw が終わると、実行の前の値に戻る）。
//=============================================================================
class FWorldScope
{
public:
	FWorldScope( FDrawContext& InCommandList, const FVillageState& State )
		: CommandList( InCommandList )
		, Clip( InCommandList, FVillageState::ViewLeft, FVillageState::ViewTop, FVillageState::ViewLeft + FVillageState::ViewWidth, FVillageState::ViewTop + FVillageState::ViewHeight )
	{
		CommandList.Transform2D( State.GetWorldMatrix() );
	}

	~FWorldScope()
	{
		CommandList.ResetTransform2D();
	}

	FWorldScope( const FWorldScope& ) = delete;
	FWorldScope& operator=( const FWorldScope& ) = delete;

private:
	FDrawContext& CommandList;
	FClipScope Clip;
};
