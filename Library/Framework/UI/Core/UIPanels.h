// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Core/UIWidget.h"

//=============================================================================
// CUICanvasPanel
//   子を、アンカー（親の内側の矩形に対する割合）とずれ（Offsets）で、自由に置く（HUD の固定に向く）。
//   子の置き方は FUILayoutProperties の AnchorMin / AnchorMax / Offsets / Pivot（Margin と寄せ方は使わない）。
//     画面の中央に 200 x 100 : AnchorMin = AnchorMax = (0.5, 0.5)、Pivot = (0.5, 0.5)、Offsets = (0, 0, 200, 100)
//     右上の隅から 10 画素   : AnchorMin = AnchorMax = (1, 0)、Pivot = (1, 0)、Offsets = (-10, 10, 200, 40)
//     全体に、四辺 8 画素の余白: AnchorMin = (0, 0)、AnchorMax = (1, 1)、Offsets = (8, 8, 8, 8)
//   Canvas 自身の「ほしい大きさ」は、内容に関わらず 0（大きさは、親の置き方か、FixedWidth / FixedHeight で決める）。
//=============================================================================
class CUICanvasPanel : public CUIWidget
{
protected:
	FVector2 ComputeContentSize() const override;
	void ArrangeChildren( const FUIRect& ContentRect ) override;
};

//=============================================================================
// CUIStackPanel
//   子を、縦または横に、順に並べる。主軸（並べる向き）の大きさは、子のほしい大きさ（Weight が正の子は、残りを重みの比で分ける）。
//   交差軸（並べる向きと直角）の寄せ方は、子の HorizontalAlignment / VerticalAlignment（Stretch なら、いっぱいに）。
//   Collapsed の子は、場所も間隔も取らない。Hidden の子は、場所を取る。
//=============================================================================
class CUIStackPanel : public CUIWidget
{
public:
	void SetOrientation( EUIOrientation InOrientation );
	EUIOrientation GetOrientation() const { return Orientation; }
	// 子と子の間隔（画素）
	void SetSpacing( float32 InSpacing );
	float32 GetSpacing() const { return Spacing; }

protected:
	FVector2 ComputeContentSize() const override;
	void ArrangeChildren( const FUIRect& ContentRect ) override;

private:
	EUIOrientation Orientation = EUIOrientation::Vertical;
	float32 Spacing = 0.0f;
};

//=============================================================================
// CUIGridPanel
//   子を、列と行の格子に置く。子の位置は FUILayoutProperties の Column / Row / ColumnSpan / RowSpan。
//   列・行の大きさは FUITrack（Auto: 中身の大きさ / Fixed: 画素 / Weight: 残りを重みで分ける）。
//   指定の無い軸は、重み 1 の列（行）が 1 つ。子の位置が範囲の外なら、いちばん端の列（行）に置く。
//   Auto の大きさは、1 つの列（行）だけにまたがる子から決める（またがる子は、数えない）。
//   Weight の列（行）も、ほしい大きさの計算では、中の子の最大を、最小の大きさとして数える（置くときは、重みで分ける）。
//=============================================================================
class CUIGridPanel : public CUIWidget
{
public:
	void SetColumns( const TArray<FUITrack>& InColumns );
	void SetRows( const TArray<FUITrack>& InRows );
	const TArray<FUITrack>& GetColumns() const { return Columns; }
	const TArray<FUITrack>& GetRows() const { return Rows; }

protected:
	FVector2 ComputeContentSize() const override;
	void ArrangeChildren( const FUIRect& ContentRect ) override;

private:
	// 列（または行）の、実際の指定（空なら、重み 1 が 1 つ）
	static TArray<FUITrack> MakeEffectiveTracks( const TArray<FUITrack>& Tracks );
	// 子のほしい大きさから、列（または行）ごとの、内容の大きさ（1 つにまたがる子の最大）を求める
	void ComputeContentSizes( bool bColumns, int32 TrackCount, TArray<float32>& OutSizes ) const;

	TArray<FUITrack> Columns;
	TArray<FUITrack> Rows;
};
