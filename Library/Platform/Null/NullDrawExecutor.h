// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// FNullDrawStats
//   CNullDrawExecutor が数えた、実行された描画命令の記録。
//   1 つの Execute（1 つのオブジェクトの描画）の終わりに、命令の中身の長さと、対になる命令（PushTarget / PopTarget など）が揃っているかも調べる。
//=============================================================================
struct FNullDrawStats
{
	// 命令の種類の数の上限（EDrawCommand は uint8）
	static constexpr int32 NumCommandTypes = 256;

	// Execute を呼ばれた回数
	int64 NumSpans = 0;
	// 実行した命令の数（全部の種類）
	int64 NumCommands = 0;
	// 実行した命令のワード数（4 バイト単位。ヘッダーを含む）
	int64 NumWords = 0;
	// 命令の並びが壊れていた Execute の数（命令の長さの合計が、並びの長さと合わなかった）
	int64 NumBrokenSpans = 0;
	// 中身が足りない（または知らない種類の）命令の数
	int64 NumBrokenCommands = 0;
	// Push / Begin の対になる Pop / End が足りないまま終わった Execute の数（DxLib の実行側は、終わりに自分で戻すので動くが、記録する側の書き忘れ）
	int64 NumUnbalancedSpans = 0;
	// 対になる Push / Begin が無いまま、Pop / End が来た回数（DxLib の実行側は、何もせず読み飛ばす）
	int64 NumExtraPops = 0;
	// 種類ごとの実行した命令の数。添字は static_cast<int32>( EDrawCommand )
	int64 CommandCounts[NumCommandTypes] = {};

	// 指定の種類の命令を実行した数
	int64 GetCount( EDrawCommand Type ) const
	{
		return CommandCounts[static_cast<int32>( Type )];
	}

	// 壊れた並び・壊れた命令・足りない Pop・余った Pop が 1 つも無いか
	bool IsClean() const
	{
		return NumBrokenSpans == 0 && NumBrokenCommands == 0 && NumUnbalancedSpans == 0 && NumExtraPops == 0;
	}
};

//=============================================================================
// CNullDrawExecutor
//   IDrawExecutor の、DxLib を使わない実装。何も描かず、命令の数と、命令の中身の長さと、対になる命令（描画先・クリップ・マスク・カメラ・
//   ワールド変換・ライト・シャドウマップ）の組み合わせだけを調べる。--backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   - 画面は無いので、描いた結果は残らない。確かめられるのは「落ちない」「壊れた命令・対の抜けが無い」「同じ入力で同じ数の命令が出る」まで
//   - 終了するときに、統計を 1 行のログに残す（"Draw statistics: ..."）。自動の検査は、この行を読む
//   - 材質付きの効果・モデルの材質効果（Supports...）は、いつも非対応（false）
//=============================================================================
class CNullDrawExecutor : public CAppSubsystem, public IDrawExecutor
{
public:
	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// 終了するときに、統計をログに残す
	void Deinitialize() override;

	// 命令を数え、対になる命令と、命令の長さを調べる
	void Execute( const FDrawSpan& Span ) override;
	// native model poseの一時適用は行わない。
	bool SupportsModelFrameOverrides() const override { return false; }

	// ここまでの統計
	const FNullDrawStats& GetStats() const;
	// 統計を 0 に戻す
	void ResetStats();

private:
	FNullDrawStats Stats;
};
