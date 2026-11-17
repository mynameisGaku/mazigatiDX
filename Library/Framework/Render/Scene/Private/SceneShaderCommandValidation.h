// SPDX-License-Identifier: MIT
#pragma once

struct FDrawCommandView;

namespace NSceneShaderCommand
{
	// シェーダー・定数・画像の設定が、範囲内の完全な命令ならtrue。
	bool IsState( const FDrawCommandView& Command );
	// 所有コピーできるシェーダー頂点列かを検査する。長さ・有限値・添字を読む前に全体の範囲を確かめる。
	bool IsDraw( const FDrawCommandView& Command );
}
