// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIScrollBar.h"
#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUIListView
//   行が何千・何万あっても、見えている分だけの部品（行）を作って、スクロールで使い回す（仮想化）リスト。向きは縦（既定）か横。
//   行の中身（何を、どう表示するか）は、ユーザーが決める: 行の部品を作る関数と、データを行へ入れる関数を渡す。
//
//   List->SetItemFactory(
//       []() -> TSharedPtr<CUIWidget> { return MakeShared<CUILabel>(); },                     // 行の部品を作る（使い回すので、少ししか呼ばれない）
//       [&Names]( CUIWidget& Item, int32 Index, bool bSelected )                              // データを行へ入れる（行が、別の番号に使われるたびに呼ばれる）
//       {
//           static_cast<CUILabel&>( Item ).SetText( Names[Index] );
//       } );
//   List->SetItemCount( Names.Num() );
//   List->OnSelectionChanged.Add( []( CUIWidget&, int32 Index ) { ... } );
//   List->OnItemClicked.Add( []( CUIWidget&, int32 Index ) { ... } );                         // クリック・決定
//
//   - 行の大きさ（主軸。縦なら高さ、横なら幅）は、すべて同じ（SetItemHeight。既定は 32）。行ごとに違うなら、SetItemSizeProvider に、番号から大きさを返す関数を渡す
//     （データが変わって大きさも変わるときは、SetItemCount か InvalidateItemSizes を呼ぶ）。交差軸は、リストの幅（高さ）いっぱい（スクロールバーが出ているときは、その分だけ狭い）。
//   - 向き: SetOrientation( Horizontal ) で、行を横に並べる（矢印は、左右で選ぶ。ホイールも、横へ動かす）。
//   - 選択は 1 つ（SetSelectedIndex。-1 は、選択なし）。クリックで選び、フォーカス中は、縦なら上下、横なら左右の矢印で選ぶ（端では、フォーカスを外へ動かす）。
//     選んだ行は、見える位置までスクロールする。決定（Enter・A ボタン）は、選んでいる行のクリックとして扱う。
//   - データが変わったら、SetItemCount（数が変わるとき）か Refresh（中身だけ変わるとき）。行の部品を、再び入れ直す。
//   - スクロール: ホイール・スクロールバー（収まらないときだけ。つまみのドラッグと、溝のクリックの 1 ページ送りができる）は、CUIScrollBox と同じ。
//   - リストの子は、実体のある行だけ（GetNumRealizedItems）。行の部品は、自分でクリックを受ける作りにしてもよい（ボタンなど）が、
//     その場合、リストの選択ではなく、その部品が受ける。
//=============================================================================
class CUIListView : public CUIWidget
{
public:
	using FCreateItem = TCallable<TSharedPtr<CUIWidget>()>;
	using FBindItem = TCallable<void( CUIWidget& Item, int32 Index, bool bSelected )>;

	using FItemExtent = TCallable<float32( int32 Index )>;

	CUIListView();

	// 行の部品を作る関数と、データを行へ入れる関数。替えると、実体のある行を、すべて捨てて作り直す
	void SetItemFactory( const FCreateItem& InCreate, const FBindItem& InBind );

	// 行の数（0 以上）。選択が範囲の外になったら、範囲に収める（変わったら OnSelectionChanged）
	void SetItemCount( int32 InCount );
	int32 GetItemCount() const { return ItemCount; }
	// 行の大きさ（主軸。画素。1 以上。縦なら高さ、横なら幅）。すべて同じ
	void SetItemHeight( float32 InHeight );
	float32 GetItemHeight() const { return ItemHeight; }
	// 行ごとの大きさを返す関数（番号 → 主軸の画素。1 未満は 1 にする）。渡すと SetItemHeight より優先。空の関数で、すべて同じ大きさへ戻す
	void SetItemSizeProvider( const FItemExtent& InProvider );
	// 行の大きさが変わったとき、計算し直す（SetItemCount でも計算し直す）
	void InvalidateItemSizes();
	// 向き（縦・横）。替えると、スクロールを先頭へ戻す
	void SetOrientation( EUIOrientation InOrientation );
	EUIOrientation GetOrientation() const { return Orientation; }

	// 選択（-1 は、なし。範囲の外は収める）。変わったら OnSelectionChanged（bNotify が false なら起こさない）。選んだ行は、見える位置へスクロールする
	void SetSelectedIndex( int32 InIndex, bool bNotify = true );
	int32 GetSelectedIndex() const { return SelectedIndex; }

	// 実体のある行を、すべて入れ直す（データの中身だけが変わったとき）
	void Refresh();

	// スクロール（画素。0〜GetMaxScrollOffset に収める）
	void SetScrollOffset( float32 InOffset );
	float32 GetScrollOffset() const { return ScrollOffset; }
	float32 GetMaxScrollOffset() const { return NMath::Max( 0.0f, ContentExtent - ViewportExtent ); }
	// 行 Index が見える位置まで、スクロールする（見えていれば、何もしない）
	void ScrollToIndex( int32 Index );
	void SetWheelStep( float32 InStep ) { WheelStep = NMath::Max( 1.0f, InStep ); }
	void SetScrollBar( const FUIBrush& InTrack, const FUIBrush& InThumb, float32 InWidth );
	FUIRect GetTrackRect() const;
	FUIRect GetThumbRect() const;

	// 行 Index の主軸の位置（先頭から。スクロールは引かない）と大きさ。範囲の外の番号は、位置が末尾・大きさ 0
	float32 GetItemOffset( int32 Index ) const;
	float32 GetItemExtent( int32 Index ) const;
	// 行 Index の画面の矩形（スクロールを反映した位置。画面の外でもよい。範囲の外の番号は、大きさ 0）
	FUIRect GetItemRect( int32 Index ) const;
	// 画面の位置にある行の番号（なければ -1）
	int32 GetIndexAt( const FVector2& Position ) const;

	// 実体のある行の数・先頭の番号・行の部品を作った回数（性能の確認に使える）
	int32 GetNumRealizedItems() const { return RealizedIndices.Num(); }
	int32 GetFirstRealizedIndex() const { return RealizedIndices.IsEmpty() ? -1 : FirstIndex; }
	int32 GetNumCreatedItems() const { return NumCreated; }

	// 選択が変わったとき（引数: このリスト・新しい番号。-1 は、選択なし）
	TUIEvent<CUIWidget&, int32> OnSelectionChanged;
	// 行がクリックされた・決定されたとき（引数: このリスト・選んでいる行の番号）
	TUIEvent<CUIWidget&, int32> OnItemClicked;

protected:
	void ArrangeChildren( const FUIRect& ContentRect ) override;
	void OnPointerPressed( const FVector2& Position ) override;
	void OnPointerDragged( const FVector2& Position ) override;
	void OnPointerReleased( const FVector2& Position ) override;
	bool OwnsPointerAt( const FVector2& Position ) const override;
	bool OnNavigate( EUINavDirection Direction ) override;
	bool OnWheel( const FVector2& Position, float32 Delta ) override;
	void OnPaintOverlay( FDrawContext& CommandList, float32 EffectiveOpacity ) override;

private:
	bool IsScrollBarShown() const { return ScrollBarWidth > 0.0f && GetMaxScrollOffset() > 0.0f; }
	bool IsVertical() const { return Orientation == EUIOrientation::Vertical; }
	FUIRect GetViewRect() const { return GetRect().Deflate( GetLayout().Padding ); }
	// 全体の主軸の大きさ・主軸の位置から行の番号（範囲の外は、先頭より前なら 0、末尾より後なら最後。行が無ければ -1）
	float32 TotalExtent() const;
	int32 IndexAtOffset( float32 Offset ) const;
	void EnsureOffsets() const;
	// 番号 Index の行が、いま実体にあれば、その部品（なければ null）
	CUIWidget* FindRealized( int32 Index ) const;
	// 実体のある行を、すべて捨てる（部品は、使い回しの置き場へ）
	void ReleaseAll();
	void BindIndex( int32 Index );

	FCreateItem CreateItem;
	FBindItem BindItem;
	int32 ItemCount = 0;
	float32 ItemHeight = 32.0f;
	EUIOrientation Orientation = EUIOrientation::Vertical;
	FItemExtent SizeProvider;
	mutable TArray<float32> ItemOffsets;   // 行ごとの大きさを使うときの、先頭からの位置（行の数 + 1 個。最後が全体の大きさ）
	mutable bool bOffsetsValid = false;
	NUIScrollBar::FDrag Drag;
	bool bIgnoreNextClick = false;         // スクロールバーを押した操作は、行のクリックにしない
	int32 SelectedIndex = -1;
	float32 ScrollOffset = 0.0f;
	float32 ContentExtent = 0.0f;
	float32 ViewportExtent = 0.0f;
	float32 WheelStep = 48.0f;
	FUIBrush TrackBrush;
	FUIBrush ThumbBrush;
	float32 ScrollBarWidth = 0.0f;

	// 実体のある行: 子の並びと同じ順で、番号を持つ。先頭の番号は FirstIndex
	TArray<int32> RealizedIndices;
	int32 FirstIndex = 0;
	TArray<TSharedPtr<CUIWidget>> Pool;
	int32 NumCreated = 0;
};
