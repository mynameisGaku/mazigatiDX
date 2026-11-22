// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"
#include "Library/Platform/Render/Canvas/CanvasTargetScopes.h"

// 2D の役割のどれにも当たらない命令（共有のシェーダーメッシュ・シェーダー・材質付きの3D・モデルの姿勢・基本の3D）を、3D とシェーダーの実行先へ渡す。
// 描けなかった命令だけを飛ばして、続きを描く。
class CCanvasSceneCommands
{
public:
	CCanvasSceneCommands( FCanvasExecution& InRun, CCanvasTargetScopes& InTargetScopes );
	// 命令を実行する。描けなければ、警告を出して、その命令だけを飛ばす。
	void Execute( const FDrawCommandView& View );

private:
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	CCanvasTargetScopes& TargetScopes; // 出力を外したときに、1つの出力のパスへ戻すために使う。
};
