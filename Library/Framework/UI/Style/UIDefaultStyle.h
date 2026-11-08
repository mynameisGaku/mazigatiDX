// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Widgets/UIControlWidgets.h"

class CAssetSubsystem;

//=============================================================================
// FUIDefaultStyle
//   エンジン組み込みの画像（EngineContent/UI。"Engine:/UI/..." で読む）から作る、部品の既定の見た目。
//   そのまま使っても、一部を差し替えても、まったく別のブラシ（マテリアル・コールバックなど）に替えてもよい。
//
//   FUIDefaultStyle Style;
//   FUIDefaultStyle::Load( *Assets, Style );
//   Button->SetStyle( Style.Button );
//   Window->SetBackground( Style.Window );
//
//   9 分割の余白は、EngineContent/UI/manifest.json から読む（読めなければ、画像の大きさの 1/4 を余白にする）。
//   画像が見つからなかった項目は、空のブラシのまま（Load は false を返すが、読めた分は入っている）。
//=============================================================================
struct FUIDefaultStyle
{
	FUIButtonStyle Button;
	FUICheckBoxStyle CheckBox;
	FUISliderStyle Slider;

	FUIBrush Window;            // 窓（9 分割）
	FUIBrush Panel;             // 半透明のパネル（9 分割）
	FUIBrush Frame;             // 枠だけ（9 分割。フォーカスの枠などに）
	FUIBrush Tooltip;           // ツールチップの背景（9 分割）
	FUIBrush TextBox;           // 入力欄の背景（9 分割）
	FUIBrush TextBoxFocused;    // フォーカス中の入力欄の背景（9 分割）
	FUIBrush Shadow;            // 窓の下に敷く影（9 分割。窓より、四方に大きく描く）
	FUIBrush ProgressBack;      // プログレスバーの背景（9 分割）
	FUIBrush ProgressFill;      // プログレスバーの中身（9 分割）
	FUIMargin ProgressInset = FUIMargin( 2.0f );   // 中身を、背景から内側へ縮める量

	// 画像から、既定の見た目を作る。1 つでも読めなかったら false
	static bool Load( CAssetSubsystem& Assets, FUIDefaultStyle& OutStyle );
};
