// SPDX-License-Identifier: MIT
#include "DxLibDisplayInfoSubsystem.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/System/DisplayLayout.h"

REGISTER_SUBSYSTEM( CDxLibDisplayInfoSubsystem )

namespace
{
	// Windows 8.1 以降の GetDpiForMonitor（Shcore.dll）。古い環境でも動くように、使うときに読み込む。無ければ nullptr
	using FGetDpiForMonitor = HRESULT( WINAPI* )( HMONITOR, int, UINT*, UINT* );

	FGetDpiForMonitor FindGetDpiForMonitor()
	{
		static FGetDpiForMonitor Function = []() -> FGetDpiForMonitor
		{
			HMODULE Library = LoadLibraryW( L"Shcore.dll" );
			return Library != nullptr ? reinterpret_cast<FGetDpiForMonitor>( GetProcAddress( Library, "GetDpiForMonitor" ) ) : nullptr;
		}();
		return Function;
	}

	// 表示の拡大率（1.0 = 96 dpi）。調べられなければ 0
	float32 GetMonitorDpiScale( HMONITOR Monitor )
	{
		const FGetDpiForMonitor GetDpi = FindGetDpiForMonitor();
		UINT DpiX = 0;
		UINT DpiY = 0;
		if ( GetDpi == nullptr || Monitor == nullptr || FAILED( GetDpi( Monitor, 0, &DpiX, &DpiY ) ) || DpiX == 0 )
		{
			return 0.0f;
		}
		return static_cast<float32>( DpiX ) / 96.0f;
	}

	// 一覧の i 番目を読む。読めなければ false
	bool ReadDisplay( int32 Index, FDisplayInfo& Out )
	{
		int RectX = 0;
		int RectY = 0;
		int SizeX = 0;
		int SizeY = 0;
		int IsPrimary = 0;
		int Refresh = 0;
		if ( Index < 0 || Index >= GetDisplayNum() || GetDisplayInfo( Index, &RectX, &RectY, &SizeX, &SizeY, &IsPrimary, &Refresh ) != 0 )
		{
			return false;
		}
		FDisplayInfo Info;
		Info.Left = RectX;
		Info.Top = RectY;
		Info.Width = SizeX;
		Info.Height = SizeY;
		Info.RefreshRate = Refresh;
		Info.bPrimary = IsPrimary != 0;
		Info.Name = FString::Printf( "Display %d", Index + 1 );

		// 名前と拡大率は、ディスプレイの中央にあるモニターから調べる
		POINT Center;
		Center.x = RectX + SizeX / 2;
		Center.y = RectY + SizeY / 2;
		HMONITOR Monitor = MonitorFromPoint( Center, MONITOR_DEFAULTTONEAREST );
		MONITORINFOEXW MonitorInfo = {};
		MonitorInfo.cbSize = sizeof( MonitorInfo );
		if ( Monitor != nullptr && GetMonitorInfoW( Monitor, &MonitorInfo ) )
		{
			Info.Name = FString( MonitorInfo.szDevice );
		}
		Info.DpiScale = GetMonitorDpiScale( Monitor );
		Out = Info;
		return true;
	}
}

bool CDxLibDisplayInfoSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DxLib;
}

void CDxLibDisplayInfoSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// DxLib の初期化後に調べるので、プラットフォームを先に初期化させる
	Collection.InitializeDependency<IPlatform>();
}

int32 CDxLibDisplayInfoSubsystem::GetDisplayCount() const
{
	const int Count = GetDisplayNum();
	return Count > 0 ? Count : 0;
}

int32 CDxLibDisplayInfoSubsystem::GetPrimaryDisplayIndex() const
{
	for ( int32 Index = 0; Index < GetDisplayCount(); ++Index )
	{
		FDisplayInfo Info;
		if ( ReadDisplay( Index, Info ) && Info.bPrimary )
		{
			return Index;
		}
	}
	return -1;
}

bool CDxLibDisplayInfoSubsystem::GetDisplay( int32 Index, FDisplayInfo& Out ) const
{
	return ReadDisplay( Index, Out );
}

bool CDxLibDisplayInfoSubsystem::GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const
{
	if ( Index < 0 || Index >= GetDisplayCount() )
	{
		return false;
	}
	const int Count = GetDisplayModeNum( Index );
	if ( Count <= 0 )
	{
		return false;
	}
	TArray<FDisplayMode> Modes;
	for ( int32 ModeIndex = 0; ModeIndex < Count; ++ModeIndex )
	{
		const DISPLAYMODEDATA Data = GetDisplayMode( ModeIndex, Index );
		FDisplayMode Mode;
		Mode.Width = Data.Width;
		Mode.Height = Data.Height;
		Mode.RefreshRate = Data.RefreshRate;
		// 同じ解像度・リフレッシュレートの重複（色数違い）は 1 つにする
		bool bDuplicate = false;
		for ( const FDisplayMode& Existing : Modes )
		{
			bDuplicate = bDuplicate || ( Existing.Width == Mode.Width && Existing.Height == Mode.Height && Existing.RefreshRate == Mode.RefreshRate );
		}
		if ( !bDuplicate && Mode.Width > 0 && Mode.Height > 0 )
		{
			Modes.Add( Mode );
		}
	}
	if ( Modes.IsEmpty() )
	{
		return false;
	}
	Out = MoveTemp( Modes );
	return true;
}

int32 CDxLibDisplayInfoSubsystem::GetWindowDisplayIndex() const
{
	const HWND Window = GetMainWindowHandle();
	RECT Rect = {};
	if ( Window == nullptr || !GetWindowRect( Window, &Rect ) )
	{
		return -1;
	}
	FDisplayLayout Layout;
	TArray<FDisplayInfo> Displays;
	for ( int32 Index = 0; Index < GetDisplayCount(); ++Index )
	{
		FDisplayInfo Info;
		if ( ReadDisplay( Index, Info ) )
		{
			Displays.Add( Info );
		}
	}
	Layout.SetDisplays( Displays );
	// ウィンドウの中心がいるディスプレイ。どれにも無ければ、いちばん近いもの
	const int32 CenterX = ( Rect.left + Rect.right ) / 2;
	const int32 CenterY = ( Rect.top + Rect.bottom ) / 2;
	const int32 Found = Layout.FindDisplayContaining( CenterX, CenterY );
	return Found >= 0 ? Found : Layout.FindNearestDisplay( CenterX, CenterY );
}

float32 CDxLibDisplayInfoSubsystem::GetWindowDpiScale() const
{
	const HWND Window = GetMainWindowHandle();
	if ( Window == nullptr )
	{
		return 1.0f;
	}
	const UINT Dpi = GetDpiForWindow( Window );
	return Dpi > 0 ? static_cast<float32>( Dpi ) / 96.0f : 1.0f;
}
