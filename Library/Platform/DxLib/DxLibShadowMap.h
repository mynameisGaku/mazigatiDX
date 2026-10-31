// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// FDxLibShadowMap
//   1 回の Execute の間に行った、シャドウマップ（影の絵）への描画と、影を受けるスロットを覚えて、終わりに元に戻す。
//   DxLib のシャドウマップは、ShadowMap_DrawSetup から ShadowMap_DrawEnd の間だけ、描画先・カメラを影の絵のものに替える（実測: 終わると、描画先・カメラ・クリップ・ブレンドなどは元に戻る）。
//   その間に、描画先・カメラ・クリップ・Clear などを変えると、影の絵が壊れる（実測: ClearDrawScreen で全面が影になり、カメラを替えると影が出ず、SetDrawScreen で ShadowMap_DrawEnd が失敗する）ので、
//   IsBlockedInPass の命令は、影の絵を描いている間は実行しない。
//   DxLib のシャドウマップは入れ子にできない（実測: 描いている間の ShadowMap_DrawSetup は、同じ影なら失敗し、別の影なら切り替わるが、ShadowMap_DrawEnd は 1 回で全部終わる）ので、
//   描いている間の BeginShadowMap は無視し、対の EndShadowMap も無視する。
//   影の絵を描けない（BeginShadowMap のハンドルが無効・値が使えない・ShadowMap_DrawSetup が失敗した）ときは、対の EndShadowMap まで、描く命令を飛ばす（IsSkippingDraws）。
//
//   Apply: シャドウマップの命令（BeginShadowMap / EndShadowMap / UseShadowMap / PreviewShadowMap）を DxLib へ反映する
//   Restore: 描いている途中なら終わらせて、使っているスロットを外す
//=============================================================================
class FDxLibShadowMap
{
public:
	// 状態だけを変える命令か（クリップ範囲が空のあいだも実行する）
	static bool IsStateCommand( EDrawCommand Type );

	// 影の絵を描いている間は実行しない命令か（描画先・カメラ・クリップ・マスク・全体を消す命令・画像の加工）
	static bool IsBlockedInPass( EDrawCommand Type );

	// 命令を実行する。シャドウマップの命令でなければ、何もせず false
	bool Apply( const FDrawCommandView& Command );

	// 描いている途中なら終わらせて、使っているスロットを外す
	void Restore();

	// 影の絵を描いている間か（描けなかった分も含む。対の EndShadowMap まで）
	bool IsInPass() const { return State != EState::Idle; }

	// 描く命令を飛ばすべきか（影の絵を描けなかった間）
	bool IsSkippingDraws() const { return State == EState::Skipping; }

private:
	enum class EState : uint8
	{
		Idle,
		// DxLib が影の絵を描いている（ShadowMap_DrawSetup が成功した）
		Drawing,
		// 描けない。対の EndShadowMap まで、描く命令を飛ばす
		Skipping,
	};

	EState State = EState::Idle;

	// 描いている間に来た BeginShadowMap の数（入れ子。対の EndShadowMap と相殺する）
	int NumNestedBegins = 0;

	// UseShadowMap で使っているスロット（終わりに外す）
	bool bSlotUsed[NDrawShadowMap::NumSlots] = {};
};
