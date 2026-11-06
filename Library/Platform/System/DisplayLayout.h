// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Platform/System/DisplayTypes.h"

//=============================================================================
// FDisplayLayout
//   ディスプレイの並び（複数のモニターの配置）を持ち、「この点はどのディスプレイか」「ウィンドウを中央に置く位置」を求めるクラス。
//   バックエンドに依らない（ディスプレイの一覧だけを渡す）。IDisplayInfo / IWindowControl を実装するバックエンドが共通で使う。
//
//   FDisplayLayout Layout;
//   Layout.SetDisplays( Displays );
//   int32 Index = Layout.FindDisplayContaining( WindowX, WindowY );
//   Layout.GetCenteredPosition( Index, WindowWidth, WindowHeight, X, Y );
//=============================================================================
class FDisplayLayout
{
public:
	// ディスプレイの一覧を置き換える
	void SetDisplays( const TArray<FDisplayInfo>& InDisplays );

	const TArray<FDisplayInfo>& GetDisplays() const
	{
		return Displays;
	}

	// 主ディスプレイの番号。無ければ（一覧が空でなければ）先頭、空なら -1
	int32 GetPrimaryIndex() const;

	// 点 (X, Y) を含むディスプレイの番号。どれにも含まれなければ -1
	int32 FindDisplayContaining( int32 X, int32 Y ) const;

	// 点 (X, Y) にいちばん近いディスプレイの番号（含まれていればそれ）。一覧が空なら -1
	int32 FindNearestDisplay( int32 X, int32 Y ) const;

	// 幅 Width・高さ Height のウィンドウを、DisplayIndex のディスプレイの中央に置く左上の位置。
	// ウィンドウがディスプレイより大きいときは、左上をディスプレイの左上に合わせる。番号が範囲の外なら false
	bool GetCenteredPosition( int32 DisplayIndex, int32 Width, int32 Height, int32& OutX, int32& OutY ) const;

private:
	TArray<FDisplayInfo> Displays;
};
