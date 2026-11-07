// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/UI/Brush/UIBrush.h"
#include "Library/Framework/UI/Core/UITypes.h"

//=============================================================================
// FUILayoutProperties
//   部品の置き方の指定。どの値が効くかは、親の種類で決まる（効かない値は無視される）。
//     共通      : Margin（外側の余白）・Padding（内側の余白）・MinSize / MaxSize・FixedWidth / FixedHeight
//     ふつう・Stack・Grid : HorizontalAlignment / VerticalAlignment（置き場所の中での寄せ方）
//     Stack     : Weight（0 は内容の大きさ。正なら、残りを重みの比で分ける）
//     Grid      : Column / Row / ColumnSpan / RowSpan
//     Canvas    : AnchorMin / AnchorMax / Offsets / Pivot（Margin と寄せ方は使わない）
//=============================================================================
struct FUILayoutProperties
{
	// Canvas の中での位置。アンカーは親の内側の矩形に対する割合（0〜1）。
	//   AnchorMin == AnchorMax の軸: 点に置く。Offsets.Left / Top = 点からのずれ、Offsets.Right / Bottom = 幅 / 高さ（0 以下なら内容の大きさ）。
	//                                Pivot（0〜1）の点が、アンカーの点＋ずれに来る。
	//   AnchorMin != AnchorMax の軸: 広げる。Offsets.Left / Top = 左 / 上のアンカーからの余白、Right / Bottom = 右 / 下のアンカーからの余白。
	FVector2 AnchorMin = FVector2( 0.0f, 0.0f );
	FVector2 AnchorMax = FVector2( 0.0f, 0.0f );
	FUIMargin Offsets;
	FVector2 Pivot = FVector2( 0.0f, 0.0f );

	FUIMargin Margin;
	FUIMargin Padding;
	EUIAlignment HorizontalAlignment = EUIAlignment::Stretch;
	EUIAlignment VerticalAlignment = EUIAlignment::Stretch;
	FVector2 MinSize = FVector2( 0.0f, 0.0f );
	FVector2 MaxSize = FVector2( NUI::Unbounded, NUI::Unbounded );
	float32 FixedWidth = -1.0f;  // 0 以上なら、内容に関わらず、この幅（寄せ方が Stretch でも広げない）
	float32 FixedHeight = -1.0f; // 0 以上なら、内容に関わらず、この高さ
	float32 Weight = 0.0f;
	int32 Column = 0;
	int32 Row = 0;
	int32 ColumnSpan = 1;
	int32 RowSpan = 1;
};

//=============================================================================
// CUIWidget
//   UI の部品の土台。部品は木になり、レイアウトと当たり判定を持つ（描画・入力・DxLib は、ここでは知らない）。
//   そのままでも「重ねる箱」として使える（子はすべて、内側の矩形いっぱいに置かれる）。
//
//   レイアウトは 2 段階:
//     Measure（子から親へ）: 各部品の「ほしい大きさ」（DesiredSize。外側の余白を含む）を決める。
//     Arrange（親から子へ）: 親が決めた置き場所（Slot）の中に、寄せ方・余白・最小 / 最大を使って、実際の矩形（GetRect）を決める。
//   見た目: 背景のブラシ（FUIBrush）と不透明度（子孫にも掛かる）を持ち、Paint が、木の描画命令を FDrawContext へ記録する（描く順は、背景 → OnPaint → 子）。
//   レイアウトは変更のあった木だけ計算し直す（UpdateLayout が、ダーティフラグと領域の変化を見て、必要なときだけ実行する）。
//
//   TSharedPtr<CUIWidget> Root = MakeShared<CUIWidget>();
//   Root->AddChild( Child );
//   Root->UpdateLayout( FUIRect( 0, 0, 1280, 720 ) );
//   CUIWidget* Hit = Root->HitTest( FVector2( 100, 100 ) );
//=============================================================================
class CUIWidget
{
public:
	CUIWidget() = default;
	virtual ~CUIWidget();
	CUIWidget( const CUIWidget& ) = delete;
	CUIWidget& operator=( const CUIWidget& ) = delete;

	// --- 名前・木 ---------------------------------------------------------
	void SetName( const FString& InName ) { Name = InName; }
	const FString& GetName() const { return Name; }

	CUIWidget* GetParent() const { return Parent; }
	const TArray<TSharedPtr<CUIWidget>>& GetChildren() const { return Children; }

	// 子を末尾に足す（すでに別の親の子なら、そこから外す）。null・自分自身・自分の祖先は足せず、false
	bool AddChild( const TSharedPtr<CUIWidget>& Child );
	// 子を Index の位置に足す（範囲の外なら末尾）
	bool InsertChild( const TSharedPtr<CUIWidget>& Child, int32 Index );
	bool RemoveChild( const CUIWidget* Child );
	void RemoveAllChildren();

	// 名前で探す（自分と子孫から、深さ優先で最初の 1 つ）
	CUIWidget* FindByName( const FString& InName );

	// --- 見え方・当たり判定 -------------------------------------------------
	void SetVisibility( EUIVisibility InVisibility );
	EUIVisibility GetVisibility() const { return Visibility; }
	// 当たり判定の対象か（false なら、この部品は当たらず、下の部品へ通す。子は別に判定される）
	void SetHitTestable( bool bInHitTestable ) { bHitTestable = bInHitTestable; }
	bool IsHitTestable() const { return bHitTestable; }
	// 子を、この部品の矩形でクリップするか（true なら、矩形の外は、子にも当たらない）
	void SetClipChildren( bool bInClipChildren ) { bClipChildren = bInClipChildren; }
	bool IsClippingChildren() const { return bClipChildren; }

	// 点に当たる、いちばん手前の部品（Visible で、HitTestable で、矩形の中にあるもの）。なければ null。子は、あとのものほど手前
	CUIWidget* HitTest( const FVector2& Point );

	// --- 見た目 -----------------------------------------------------------
	// 背景のブラシ（部品の矩形いっぱいに描く。既定は何も描かない）
	void SetBackground( const FUIBrush& InBackground ) { Background = InBackground; }
	const FUIBrush& GetBackground() const { return Background; }
	// 不透明度（0〜1。範囲の外は収める）。子孫にも掛かる（親の不透明度との積）
	void SetOpacity( float32 InOpacity ) { Opacity = NMath::Clamp( InOpacity, 0.0f, 1.0f ); }
	float32 GetOpacity() const { return Opacity; }

	// この部品と子孫を描く命令を、CommandList へ記録する（ルートに対して呼ぶ。先に UpdateLayout で矩形を決めておく）。
	// Visible の部品だけを描く（Hidden / Collapsed は、子ごと描かない）。クリップする部品は、子を矩形でクリップする。
	// 最後に、ブレンドと画像に掛ける色を、元に戻す
	void Paint( FDrawContext& CommandList );

	// --- レイアウト -------------------------------------------------------
	const FUILayoutProperties& GetLayout() const { return Layout; }
	void SetLayout( const FUILayoutProperties& InLayout );

	// 木全体のレイアウトを、Area の中で計算する（ルートに対して呼ぶ）。変更が無く、Area も同じなら、何もせず false
	bool UpdateLayout( const FUIRect& Area );

	// レイアウトの計算し直しが要るか（自分か子孫が変わったあと、UpdateLayout までの間は true）
	bool IsLayoutDirty() const { return bLayoutDirty; }
	// 計算し直しを求める（祖先にも伝わる）
	void MarkLayoutDirty();

	// 「ほしい大きさ」（外側の余白を含む。Measure のあとで有効）
	const FVector2& GetDesiredSize() const { return DesiredSize; }
	// 実際の矩形（外側の余白を含まない。画面の座標。Arrange のあとで有効）
	const FUIRect& GetRect() const { return Rect; }

	// 下の 2 つは UpdateLayout の中身。パネルが、子を置くときにも使う
	void Measure();
	// 置き場所 Slot（外側の余白を含む）の中に、寄せ方・余白・最小 / 最大に従って置く
	void Arrange( const FUIRect& Slot );
	// 余白・寄せ方・最小 / 最大を使わず、矩形をそのまま Final にして置く（Canvas のように、親が矩形を直接決めるとき）
	void ArrangeExact( const FUIRect& Final );

protected:
	// 内容（子）が、ほしい大きさ。内側の余白（Padding）は含めない。既定は、子のほしい大きさの最大（重ねる箱）
	virtual FVector2 ComputeContentSize() const;
	// 内側の矩形 ContentRect（Padding を引いたもの）の中に、子を置く。既定は、子をすべて、いっぱいに置く
	virtual void ArrangeChildren( const FUIRect& ContentRect );

	// 背景を描いたあとに、部品が自分の中身（文字・画像など）を描く。EffectiveOpacity は、親から掛け合わせた不透明度。既定は何もしない
	virtual void OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity );

private:
	void ClearLayoutDirty();
	void PaintRecursive( FDrawContext& CommandList, float32 ParentOpacity );

	FString Name;
	CUIWidget* Parent = nullptr;
	TArray<TSharedPtr<CUIWidget>> Children;
	FUILayoutProperties Layout;
	EUIVisibility Visibility = EUIVisibility::Visible;
	FUIBrush Background;
	float32 Opacity = 1.0f;
	bool bHitTestable = true;
	bool bClipChildren = false;
	bool bLayoutDirty = true;
	bool bHasArranged = false;
	FUIRect LastArea;
	FVector2 DesiredSize = FVector2( 0.0f, 0.0f );
	FUIRect Rect;
};
