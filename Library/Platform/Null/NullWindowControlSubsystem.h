// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Platform/IWindowControl.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullWindowControlSubsystem
//   IWindowControl の、実機のウィンドウを触らない実装。位置・倍率・フォーカスを覚えるだけ（--backend=null のときだけ作られる）。
//   - 初めの位置は (0, 0)、倍率は 1.0、フォーカスは有り
//   - IPlatform がある場合は、通常のウィンドウ（Windowed）のときだけ位置と倍率を変えられる（実機と同じ）。IPlatform が無ければ変えられる
//   - ディスプレイの並びは IDisplayInfo から取る（無ければ MoveWindowToDisplay は false）
//=============================================================================
class CNullWindowControlSubsystem : public CAppSubsystem, public IWindowControl
{
public:
	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	bool GetWindowPosition( int32& OutX, int32& OutY ) const override;
	bool SetWindowPosition( int32 X, int32 Y ) override;
	bool MoveWindowToDisplay( int32 DisplayIndex ) override;
	float64 GetWindowScale() const override;
	bool SetWindowScale( float64 Scale ) override;
	bool IsWindowFocused() const override;

	// フォーカスの有無を決める
	void SetWindowFocused( bool bFocused );
	// MoveWindowToDisplay で中央を求めるときのウィンドウの大きさを決める（初めは 1280 x 720）
	void SetWindowSize( int32 Width, int32 Height );

private:
	int32 PositionX = 0;
	int32 PositionY = 0;
	float64 Scale = 1.0;
	bool bFocused = true;
	int32 WindowWidth = 1280;
	int32 WindowHeight = 720;
};
