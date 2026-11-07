// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Common/PlatformerStatus.h"

//=============================================================================
// FPlatformerOptions
//   Platformer2D / Platformer3D の、起動の引数（画面の確認と、検査のため）。
//     --autoplay     自動で遊ぶ
//     --form=big     最初の姿を、おおきいに（--form=fire で、ファイア）
//     --star         最初から、スターの無敵
//=============================================================================
struct FPlatformerOptions
{
	bool bAutoPlay = false;
	EPlatformerForm Form = EPlatformerForm::Small;
	bool bStar = false;

	static FPlatformerOptions FromCommandLine();

	// 最初の姿とスターを、状態に反映する
	void ApplyTo( FPlatformerStatus& Status ) const;
};
