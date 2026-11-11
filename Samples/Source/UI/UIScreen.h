// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/UI/Animation/UIAnimator.h"
#include "Library/Framework/UI/Binding/UIBindingSet.h"
#include "Library/Framework/UI/Core/UIContext.h"
#include "Library/Framework/UI/Core/UIScreenStack.h"
#include "Library/Framework/UI/Markup/UIMarkupDocument.h"
#include "Library/Framework/UI/Core/UIPanels.h"
#include "Library/Framework/UI/Style/UIDefaultStyle.h"
#include "Library/Framework/UI/Style/UIThemeLoader.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDialog.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"
#include "Library/Framework/UI/Widgets/UIListView.h"
#include "Library/Framework/UI/Widgets/UIScrollBox.h"
#include "Library/Framework/UI/Widgets/UITextBox.h"
#include "Source/UI/SettingsViewModel.h"
#include "Library/Framework/UI/Widgets/UIRenderTargetView.h"

//=============================================================================
// CUIScreen
//   UI の部品で作った画面（1280 x 720）。エンジン組み込みの画像（Engine:/UI・Engine:/Icons）の既定のスタイルで、
//     左: 設定の窓（チェックボックス・スライダー・プログレスバー・ボタン・ラベル）
//     右上: 描画先（レンダーターゲット）を表示する部品（動くミニマップ。クリックした位置に印が付く）
//     右下: スクロール枠の中のアイコンの一覧（ホイールでスクロール）と、1 万行のリスト（見える分だけ作る。クリック・上下の矢印で選ぶ）
//   入力は、ゲームの入力（IInput・IGamepadInput）から NUIInput::MakeFrame で UI の入力に直して、CUIContext へ渡す。
//   左の窓は、ビューモデル（FSettingsViewModel）に結んである（MVVM）: スライダー・チェックボックス・入力欄は双方向、文字・バーは一方向、Reset は操作。
//   左の窓には、名前を入れる入力欄（文字入力は ITextInput から。日本語の変換も使える）。Reset を押すと、確認のダイアログ（画面のスタックに積む）が出る。
//   設定の窓は、出てくるときに、ふわっと下から現れ、ボタンは、乗ると少し大きく、押すと少し小さくなる（マークアップの <Animation> と HoverScale・PressedScale）。
//   マウス・矢印キー・Tab・Enter・Escape・パッドで操作できる。
//=============================================================================
class CUIScreen : public CGameObject
{
public:
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 最初の Tick で、部品の木を作る（アセットなどが、シーンに入ってから使えるため）
	void Build();
	TSharedPtr<CUIWidget> BuildSettingsWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body, const TSharedPtr<CFont>& Title );
	void ReloadSettingsWindow();
	void ReplaceSettingsWindow();
	void NextTheme();
	TSharedPtr<CUIWidget> BuildViewWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body );
	TSharedPtr<CUIWidget> BuildIconWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body );
	TSharedPtr<CUILabel> MakeLabel( const TSharedPtr<CFont>& Font, const char* Text, const FColor32& Color = FColor32( 235, 238, 245 ) );
	TSharedPtr<CUIButton> MakeButton( const TSharedPtr<CFont>& Font, const char* Text, float32 Width );
	void SetStatus( const char* Text );
	void AskReset();

	bool bBuilt = false;
	FUIDefaultStyle Style;
	TSharedPtr<CUICanvasPanel> Root;
	FSettingsViewModel ViewModel;                   // 設定の窓のビューモデル（結び付けより先に、宣言する）
	CUIBindingSet Bindings;                         // 部品とビューモデルの結び付け
	CUIBindingSet SettingsBindings;                 // 設定の窓（マークアップ）の結び付け
	FUIMarkupContext MarkupContext;                 // マークアップで使う名前の表
	CUIAnimator Animator;                           // 部品の動き（マークアップの <Animation> と、ホバー・押下の動き）
	CUIMarkupDocument SettingsDocument;             // 設定の窓のマークアップ（Assets/UI/Settings.gxui。保存すると作り直す）
	TSharedPtr<CUIWidget> SettingsWindow;
	float32 ReloadTimer = 0.0f;
	TArray<TSharedPtr<FUITheme>> Themes;            // 基本のテーマと、色だけ変えた子のテーマ
	int32 ThemeIndex = 0;
	bool bThemeChangePending = false;
	bool bOpenDropdownPending = false;   // 撮影用: 最初の更新のあとで、ドロップダウンを開く
	CUIScreenStack Screens;                         // 画面のスタック: 下は、この画面。確認のダイアログが、上に積まれる
	TSharedPtr<CFont> BodyFont;
	TSharedPtr<CFont> TitleFont;
	TSharedPtr<CUIRenderTargetView> MinimapView;
	TSharedPtr<CUIListView> NameList;

	float32 ElapsedTime = 0.0f;
	FVector2 Marker = FVector2( -1.0f, -1.0f );   // ミニマップに付けた印（描画先の座標。負なら、まだ無い）
	int32 CreatedTargets = 0;
};
