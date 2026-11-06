// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ディスプレイ（モニター）の値の型（バックエンドに依らない）
//   IDisplayInfo が返す。座標はデスクトップ全体の座標（主ディスプレイの左上が原点。左や上にあるディスプレイは負になる）。
//=============================================================================

// 切り替えられる画面の解像度とリフレッシュレートの組
struct FDisplayMode
{
	int32 Width = 0;
	int32 Height = 0;
	// 1 秒あたりの更新回数（Hz）。分からなければ 0
	int32 RefreshRate = 0;
};

// ディスプレイ 1 台
struct FDisplayInfo
{
	// 表示用の名前（分からなければ空）
	FString Name;
	// デスクトップ上の位置と大きさ（ピクセル）
	int32 Left = 0;
	int32 Top = 0;
	int32 Width = 0;
	int32 Height = 0;
	// 現在のリフレッシュレート（Hz）。分からなければ 0
	int32 RefreshRate = 0;
	// 主ディスプレイか
	bool bPrimary = false;
	// 表示の拡大率（1.0 = 100%、1.5 = 150%）。分からなければ 0
	float32 DpiScale = 0.0f;

	int32 GetRight() const
	{
		return Left + Width;
	}

	int32 GetBottom() const
	{
		return Top + Height;
	}

	// 点 (X, Y) がこのディスプレイの中か
	bool Contains( int32 X, int32 Y ) const
	{
		return X >= Left && X < GetRight() && Y >= Top && Y < GetBottom();
	}
};
