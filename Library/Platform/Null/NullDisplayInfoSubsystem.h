// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullDisplayInfoSubsystem
//   IDisplayInfo の、実機を調べない実装。プログラムが決めたディスプレイの並びを返す（--backend=null のときだけ作られる）。
//   初めは、1920x1080・60 Hz・主ディスプレイが 1 台あるものとして答える。
//
//   CNullDisplayInfoSubsystem* Info = GetAppSubsystem<CNullDisplayInfoSubsystem>();
//   Info->SetDisplays( Displays );          // 複数のモニターの並びを決める
//   Info->SetWindowDisplayIndex( 1 );       // ウィンドウは 2 台目にいることにする
//=============================================================================
class CNullDisplayInfoSubsystem : public CAppSubsystem, public IDisplayInfo
{
public:
	CNullDisplayInfoSubsystem();

	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	int32 GetDisplayCount() const override;
	int32 GetPrimaryDisplayIndex() const override;
	bool GetDisplay( int32 Index, FDisplayInfo& Out ) const override;
	bool GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const override;
	int32 GetWindowDisplayIndex() const override;
	float32 GetWindowDpiScale() const override;

	//-------------------------------------------------------------------------
	// 答えを決める
	//-------------------------------------------------------------------------
	// ディスプレイの並びを置き換える（切り替えられる解像度は、各ディスプレイの現在の解像度だけになる）
	void SetDisplays( const TArray<FDisplayInfo>& InDisplays );
	// Index 番のディスプレイの、切り替えられる解像度を決める
	void SetDisplayModes( int32 Index, const TArray<FDisplayMode>& Modes );
	void SetWindowDisplayIndex( int32 Index );
	void SetWindowDpiScale( float32 Scale );

private:
	TArray<FDisplayInfo> Displays;
	TArray<TArray<FDisplayMode>> Modes;
	int32 WindowDisplayIndex = 0;
	float32 WindowDpiScale = 1.0f;
};
