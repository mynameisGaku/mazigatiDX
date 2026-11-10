// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"

// 外部ImGuiウィンドウの表示頻度と順番を管理する。GPUと窓の作成・破棄は公式実装が担当する。
class CImGuiViewportScheduler
{
public:
	// 最後に表示した時刻を消す。ImGui文脈の終了時に呼ぶ。
	void Reset();
	// 古い表示から順に更新する。上限が0以下なら制限せず、最小化した窓は表示しない。
	// 文脈がない場合と機能が無効な場合は、次の有効化へ古い表示時刻を持ち越さずに戻る。
	void Render( int32 MaxFPS, int32 MaxPresents );
private:
	TMap<uint32, int64> LastPresentTicks; // 外部ウィンドウの番号ごとの最終表示時刻。
};
