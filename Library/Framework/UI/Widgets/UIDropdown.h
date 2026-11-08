// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Framework/UI/Core/UIScreenStack.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"
#include "Library/Framework/UI/Widgets/UIListView.h"

//=============================================================================
// CUIDropdown
//   選んでいる項目の文字と矢印を見せるボタン。押す（クリック・決定）と、下（入りきらなければ上）に、項目の一覧（CUIListView）が開く。
//   一覧は、画面のスタック（SetPopupHost で渡す）に、モーダルな画面として積む。一覧の外をクリックするか、Escape で、選ばずに閉じる。
//   項目をクリックするか、矢印で選んで Enter を押すと、その項目に決まって閉じる。
//
//   TSharedPtr<CUIDropdown> Quality = MakeShared<CUIDropdown>();
//   Quality->SetFont( Font );
//   Quality->SetDropdownStyle( Style );
//   Quality->SetPopupHost( &Screens );                       // 画面のスタック（ドロップダウンより長く生きること）
//   Quality->SetItems( { "Low", "Medium", "High" } );
//   Quality->SetSelectedIndex( 1 );
//   Quality->OnSelectionChanged.Add( []( CUIWidget&, int32 Index ) { ... } );
//
//   - 選ぶ番号は、-1（何も選んでいない。SetPlaceholder の文字を、薄く出す）か、0 以上。SetItems で、項目が減って範囲の外になったら、収める。
//   - SetPopupHost が無い（null）と、開かない。項目が無いときも、開かない。使えない（Disabled）ときも、開かない。
//   - 見た目は、閉じているとき CUIButton と同じブラシ（FUIDropdownStyle::Button）。矢印・文字は、中に置いた部品が描く。
//=============================================================================
struct FUIDropdownStyle
{
	FUIButtonStyle Button;                                   // 閉じているときの、状態ごとのブラシ
	FUIBrush Arrow;                                          // 右端の矢印（無ければ描かない。画像のブラシなど）
	FVector2 ArrowSize = FVector2( 16.0f, 16.0f );
	FUIMargin TextPadding = FUIMargin( 12.0f, 0.0f, 36.0f, 0.0f );   // 文字の内側の余白（右は、矢印の分を空ける）
	FColor32 TextColor = FColor32( 235, 238, 245, 255 );
	FColor32 PlaceholderColor = FColor32( 130, 140, 160, 255 );

	FUIBrush PopupBackground;                                // 開いた一覧の背景
	FUIMargin PopupPadding = FUIMargin( 4.0f );
	FUIBrush ItemSelected;                                   // 選んでいる項目の背景
	FColor32 ItemTextColor = FColor32( 235, 238, 245, 255 );
	FUIMargin ItemPadding = FUIMargin( 10.0f, 0.0f );
	float32 ItemHeight = 32.0f;
	int32 MaxVisibleItems = 8;                               // 一覧に、いちどに見せる項目の数（これを超えると、スクロール）
	FUIBrush ScrollTrack;
	FUIBrush ScrollThumb;
	float32 ScrollBarWidth = 0.0f;                           // 0 ならスクロールバーは出さない（ホイールでは動く）
};

class CUIDropdown : public CUIButton
{
public:
	CUIDropdown();

	void SetFont( const TSharedPtr<CFont>& InFont );
	void SetDropdownStyle( const FUIDropdownStyle& InStyle );
	const FUIDropdownStyle& GetDropdownStyle() const { return DropdownStyle; }

	void SetItems( const TArray<FString>& InItems );
	const TArray<FString>& GetItems() const { return Items; }
	// 選ぶ（-1 は、なし。範囲の外は収める）。変わったら OnSelectionChanged（bNotify が false なら起こさない）
	void SetSelectedIndex( int32 InIndex, bool bNotify = true );
	int32 GetSelectedIndex() const { return SelectedIndex; }
	// 選んでいる項目の文字（選んでいなければ空）
	FString GetSelectedText() const;
	// 何も選んでいないときに、薄く出す文字
	void SetPlaceholder( const FString& InPlaceholder );

	// 一覧を積む、画面のスタック。ドロップダウンより長く生きること
	void SetPopupHost( CUIScreenStack* InHost ) { PopupHost = InHost; }
	// 一覧が開いているか
	bool IsOpen() const;
	// 開く・閉じる（開けないときは、何もしない）
	void Open();
	void Close();

	// 選ぶ番号が変わったとき（引数: この部品・新しい番号。-1 は、なし）
	TUIEvent<CUIWidget&, int32> OnSelectionChanged;

private:
	void RefreshLabel();

	FUIDropdownStyle DropdownStyle;
	TSharedPtr<CFont> Font;
	TArray<FString> Items;
	int32 SelectedIndex = -1;
	FString Placeholder;
	TSharedPtr<CUILabel> Label;
	TSharedPtr<CUIWidget> ArrowWidget;
	CUIScreenStack* PopupHost = nullptr;
	int32 PopupId = 0;
	TSharedPtr<bool> Alive = MakeShared<bool>( true );   // 一覧のイベントが、この部品より後に呼ばれても安全なように
};
