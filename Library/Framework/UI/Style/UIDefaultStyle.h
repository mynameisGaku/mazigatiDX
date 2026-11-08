// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Style/UITheme.h"
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
	FUIMargin ProgressInset;    // 中身を、背景から内側へ縮める量（0。中身の画像が、背景の枠の内側に収まる形なので、縮めない）

	// 画像から、既定の見た目を作る。1 つでも読めなかったら false
	static bool Load( CAssetSubsystem& Assets, FUIDefaultStyle& OutStyle );

	// この見た目を、基本のテーマにする（名前は下の通り。フォントは、渡したときだけ入れる）。これの子のテーマで、変えたいところだけ上書きする
	//   ブラシ: Window Panel Frame Tooltip TextBox TextBoxFocused Shadow ProgressBack ProgressFill
	//   スタイル: Button / CheckBox / Slider / TextBox / Dropdown の "Default"（Dropdown の矢印の画像は、無い。使うときに、Arrow へ足す）
	//   色: Text TextMuted Accent Value Error    数: Gap（14）Spacing（18）
	//   フォント: Body Title
	TSharedPtr<FUITheme> MakeTheme( const TSharedPtr<CFont>& Body = TSharedPtr<CFont>(), const TSharedPtr<CFont>& Title = TSharedPtr<CFont>() ) const;
};
