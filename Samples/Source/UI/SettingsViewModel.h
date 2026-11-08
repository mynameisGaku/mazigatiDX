// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Binding/UIObservable.h"

//=============================================================================
// FSettingsViewModel
//   設定の窓のビューモデル（MVVM）。UI の部品を、いっさい知らない。窓の部品は、CUIBindingSet で、ここの値に結ばれる。
//   値のルール（音量の文字・割合、状態の文章、既定へ戻す）は、ここに書く。窓は、結ぶだけ。
//=============================================================================
struct FSettingsViewModel
{
	TObservable<float32> Volume { 50.0f };                                  // 0〜100
	TObservable<bool> Music { true };
	TObservable<bool> Sound { true };
	TObservable<FString> Name;
	TObservable<FString> Status { FString( "Use the mouse, arrow keys, Tab, Enter or a gamepad" ) };

	// 音量から作る値（音量が変わると、ここで更新する）
	TObservable<FString> VolumeText { FString( "50%" ) };
	TObservable<float32> VolumeRatio { 0.5f };

	// Reset の操作（押すと、確認を求める。確認を出す仕事は、持ち主が OnResetRequested へ入れる）
	FUICommand Reset;
	TCallable<void()> OnResetRequested;

	FSettingsViewModel()
		: Reset( [this]()
		{
			if ( OnResetRequested )
			{
				OnResetRequested();
			}
		} )
	{
		Volume.OnChanged.Add( [this]( const float32& Value )
		{
			VolumeText.Set( FString::Printf( "%d%%", static_cast<int32>( Value ) ) );
			VolumeRatio.Set( Value / 100.0f );
		} );
		Music.OnChanged.Add( [this]( const bool& bOn ) { Status.Set( bOn ? "Music on" : "Music off" ); } );
		Sound.OnChanged.Add( [this]( const bool& bOn ) { Status.Set( bOn ? "Sound effects on" : "Sound effects off" ); } );
	}

	// 既定へ戻す（名前・音量・チェック）
	void ResetToDefaults()
	{
		Volume.Set( 50.0f );
		Music.Set( true );
		Sound.Set( true );
		Name.Set( FString() );
		Status.Set( "Reset to the defaults" );
	}
};
