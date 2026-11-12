// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Style/UITheme.h"

class CAssetSubsystem;

//=============================================================================
// CUIThemeLoader: テーマのファイル（.gxuitheme。JSON）から、テーマ（FUITheme）を作る
//   色・数・ブラシ・部品のスタイル（ボタン・チェックボックス・スライダー・入力欄・ドロップダウン）を、コードではなく、ファイルに書ける。
//   フォントは、コードで足す（親のテーマに入れておけば、そこから引かれる）。
//   ファイルを保存し直したら読み直す（ホットリロード）のは、CUIThemeDocument（UIThemeDocument.h）。
//
//   {
//     "Name": "Blue",
//     "Colors":  { "Accent": "#78B4FF", "Value": "#B4DCFF" },
//     "Numbers": { "Gap": 14, "Spacing": 18 },
//     "Brushes": {
//       "Flat":   "#202830",
//       "Card":   { "Color": "#202830", "CornerRadius": 8, "OutlineColor": "@Accent", "OutlineThickness": 1.5 },
//       "Window": { "Image": "Engine:/UI/Window.png", "Mode": "NineSlice", "Margin": [ 12, 12, 12, 12 ], "Tint": "#FFFFFF" },
//       "Glow":   { "Material": "Engine:/Materials/UI/GlowButton.gxmaterial" },
//       "Framed": { "Layers": [ "Card", { "Image": "Engine:/UI/Frame.png", "Mode": "NineSlice" } ] }
//     },
//     "ButtonStyles":   { "Flat": { "Normal": "Card", "Hover": "#2C3848", "Pressed": "#182028", "Disabled": "#40444C", "FocusOverlay": "Framed" } },
//     "CheckBoxStyles": { "Flat": { "Off": "Card", "On": "@Accent" } },
//     "SliderStyles":   { "Thin": { "Extends": "Default", "TrackInset": [ 0, 8 ], "HandleSize": 18 } },
//     "TextBoxStyles":  { "Flat": { "Normal": "Card", "Focused": "Framed", "TextColor": "#FFFFFF", "TextPadding": [ 10, 4 ] } },
//     "DropdownStyles": { "Flat": { "Button": "Flat", "PopupBackground": "Card", "ItemSelected": "@Accent", "ItemHeight": 32, "MaxVisibleItems": 6 } }
//   }
//
//   TSharedPtr<FUITheme> Blue = CUIThemeLoader::Load( Assets, "UI/Themes/Blue.gxuitheme", Base, Error );   // Base の子のテーマ。失敗なら null（Error に理由）
//
// 書き方:
//   - 色: "#RGB"・"#RRGGBB"・"#RRGGBBAA"。ブラシとスタイルの中では、"@名前"（このテーマか、親のテーマの色）も書ける。
//   - ブラシ: 文字なら、色（単色の面）か、ブラシの名前（このファイルか、親のテーマ）。オブジェクトなら、次のどれか 1 つ:
//       単色     { "Color", "CornerRadius", "OutlineColor", "OutlineThickness" }
//       画像     { "Image": パス, "Mode": Stretch / Fit / Fill / Tile / NineSlice（既定は Stretch）, "Margin": 数 か [左, 上, 右, 下]（9 分割の余白。省くと、画像の 1/4）, "Tint" }
//       マテリアル { "Material": パス, "Tint" }
//       重ね     { "Layers": [ ブラシ, ... ] }（先のものが奥）
//   - "Brushes" の中の、名前での参照は、同じファイルの中なら、書く順に関係なく使える（輪になる参照は、失敗）。
//   - スタイル: "Extends": 名前 で、同じ種類のスタイル（このファイルか、親のテーマ。同じファイルの中なら、書く順に関係なく）を元にして、書いた項目だけを変える。
//     自分と同じ名前を書くと、親のテーマの、同じ名前のスタイルが元になる（"Default": { "Extends": "Default", ... } で、親の Default を、一部だけ変える）。
//     "Extends" が無ければ、書かなかった項目は、既定の値（ブラシは空）。項目は、次の通り（ブラシは、上と同じ書き方。色は "#RRGGBB" か "@名前"）:
//       ButtonStyles    ブラシ: Normal Hover Pressed Disabled FocusOverlay ／ 数: TransitionDuration
//       CheckBoxStyles  ブラシ: Off On HoverOverlay DisabledOverlay FocusOverlay ／ 数: TransitionDuration
//       SliderStyles    ブラシ: Track Fill Handle HandleHover ／ 大きさ: HandleSize ／ 余白: TrackInset FillInset ／ 数: TransitionDuration
//       TextBoxStyles   ブラシ: Normal Focused Disabled ／ 色: TextColor PlaceholderColor DisabledTextColor CaretColor SelectionColor ／ 余白: TextPadding ／ 数: TransitionDuration
//     TransitionDuration は、状態のブラシが切り替わるとき、なめらかに替わるまでの秒（0 なら、すぐ。既定）。
//       DropdownStyles  Button（ボタンのスタイルの名前か、ボタンのスタイルと同じ書き方）／ ブラシ: Arrow PopupBackground ItemSelected ScrollTrack ScrollThumb ／
//                       色: TextColor PlaceholderColor ItemTextColor ／ 余白: TextPadding PopupPadding ItemPadding ／ 大きさ: ArrowSize ／
//                       数（0 以上）: ItemHeight ScrollBarWidth ／ 1 以上の整数: MaxVisibleItems
//     大きさは、数（縦横とも）か [幅, 高さ]。余白は、数（四辺）・[左右, 上下]・[左, 上, 右, 下]。
// 失敗（JSON が壊れている・型が違う・知らない項目・見つからない名前・読めない画像やマテリアル）は、null を返し、Error に理由（英語）。
//=============================================================================
class CUIThemeLoader
{
public:
	// アセットのパスから読む（他のアセットと同じパス。"Engine:/..." も使える）。Parent は、親のテーマ（無くてもよい）
	static TSharedPtr<FUITheme> Load( CAssetSubsystem& Assets, const FString& Path, const TSharedPtr<FUITheme>& Parent, FString& OutError );

	// JSON の文字列から作る。Assets が null なら、画像のブラシは、失敗になる
	static TSharedPtr<FUITheme> LoadFromText( const FString& Json, CAssetSubsystem* Assets, const TSharedPtr<FUITheme>& Parent, FString& OutError );
};
