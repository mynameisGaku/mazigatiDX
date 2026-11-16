// SPDX-License-Identifier: MIT
#include "WindowsDisplayInfoSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/DisplayLayout.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CWindowsDisplayInfoSubsystem )

namespace
{
	// Windows 8.1 以降の GetDpiForMonitor（Shcore.dll）。使うときに読み込む。無ければ nullptr
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

	struct FMonitorEntry
	{
		HMONITOR Handle = nullptr;
		MONITORINFOEXW Info = {};
	};

	BOOL CALLBACK CollectMonitor( HMONITOR Monitor, HDC, LPRECT, LPARAM Param )
	{
		FMonitorEntry Entry;
		Entry.Handle = Monitor;
		Entry.Info.cbSize = sizeof( Entry.Info );
		if ( GetMonitorInfoW( Monitor, &Entry.Info ) )
		{
			static_cast<TArray<FMonitorEntry>*>( reinterpret_cast<void*>( Param ) )->Add( Entry );
		}
		return TRUE;
	}

	// つながっているモニターを、列挙された順に並べる。主ディスプレイが先頭になるよう、安定して並べ替える（IDisplayInfo の番号が、いつも同じ並びになる）
	TArray<FMonitorEntry> ListMonitors()
	{
		TArray<FMonitorEntry> Monitors;
		EnumDisplayMonitors( nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>( &Monitors ) );
		return Monitors;
	}

	bool ReadDisplay( const FMonitorEntry& Entry, int32 Index, FDisplayInfo& Out )
	{
		FDisplayInfo Info;
		Info.Left = Entry.Info.rcMonitor.left;
		Info.Top = Entry.Info.rcMonitor.top;
		Info.Width = Entry.Info.rcMonitor.right - Entry.Info.rcMonitor.left;
		Info.Height = Entry.Info.rcMonitor.bottom - Entry.Info.rcMonitor.top;
		Info.bPrimary = ( Entry.Info.dwFlags & MONITORINFOF_PRIMARY ) != 0;
		Info.Name = FString( Entry.Info.szDevice );
		if ( Info.Name.IsEmpty() )
		{
			Info.Name = FString::Printf( "Display %d", Index + 1 );
		}
		DEVMODEW Mode = {};
		Mode.dmSize = sizeof( Mode );
		if ( EnumDisplaySettingsW( Entry.Info.szDevice, ENUM_CURRENT_SETTINGS, &Mode ) )
		{
			Info.RefreshRate = static_cast<int32>( Mode.dmDisplayFrequency );
		}
		Info.DpiScale = GetMonitorDpiScale( Entry.Handle );
		Out = Info;
		return Info.Width > 0 && Info.Height > 0;
	}

	TArray<FDisplayInfo> ListDisplays()
	{
		TArray<FDisplayInfo> Displays;
		const TArray<FMonitorEntry> Monitors = ListMonitors();
		for ( int32 Index = 0; Index < Monitors.Num(); ++Index )
		{
			FDisplayInfo Info;
			if ( ReadDisplay( Monitors[Index], Index, Info ) )
			{
				Displays.Add( Info );
			}
		}
		return Displays;
	}
}

bool CWindowsDisplayInfoSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsDisplayInfoSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Bind( Collection.InitializeDependency<INativeWindow>() );
}

void CWindowsDisplayInfoSubsystem::Deinitialize()
{
	Window = nullptr;
}

void CWindowsDisplayInfoSubsystem::Bind( INativeWindow* InWindow )
{
	Window = InWindow;
}

int32 CWindowsDisplayInfoSubsystem::GetDisplayCount() const
{
	return ListDisplays().Num();
}

int32 CWindowsDisplayInfoSubsystem::GetPrimaryDisplayIndex() const
{
	const TArray<FDisplayInfo> Displays = ListDisplays();
	for ( int32 Index = 0; Index < Displays.Num(); ++Index )
	{
		if ( Displays[Index].bPrimary )
		{
			return Index;
		}
	}
	return -1;
}

bool CWindowsDisplayInfoSubsystem::GetDisplay( int32 Index, FDisplayInfo& Out ) const
{
	const TArray<FDisplayInfo> Displays = ListDisplays();
	if ( Index < 0 || Index >= Displays.Num() )
	{
		return false;
	}
	Out = Displays[Index];
	return true;
}

bool CWindowsDisplayInfoSubsystem::GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const
{
	const TArray<FMonitorEntry> Monitors = ListMonitors();
	if ( Index < 0 || Index >= Monitors.Num() )
	{
		return false;
	}
	TArray<FDisplayMode> Modes;
	DEVMODEW Data = {};
	Data.dmSize = sizeof( Data );
	for ( DWORD ModeIndex = 0; EnumDisplaySettingsW( Monitors[Index].Info.szDevice, ModeIndex, &Data ); ++ModeIndex )
	{
		FDisplayMode Mode;
		Mode.Width = static_cast<int32>( Data.dmPelsWidth );
		Mode.Height = static_cast<int32>( Data.dmPelsHeight );
		Mode.RefreshRate = static_cast<int32>( Data.dmDisplayFrequency );
		bool bDuplicate = false; // 色数違いの重複は 1 つにする
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

int32 CWindowsDisplayInfoSubsystem::GetWindowDisplayIndex() const
{
	const HWND Handle = Window != nullptr ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr;
	RECT Rect = {};
	if ( Handle == nullptr || !IsWindow( Handle ) || !GetWindowRect( Handle, &Rect ) )
	{
		return -1;
	}
	FDisplayLayout Layout;
	Layout.SetDisplays( ListDisplays() );
	// ウィンドウの中心がいるディスプレイ。どれにも無ければ、いちばん近いもの
	const int32 CenterX = ( Rect.left + Rect.right ) / 2;
	const int32 CenterY = ( Rect.top + Rect.bottom ) / 2;
	const int32 Found = Layout.FindDisplayContaining( CenterX, CenterY );
	return Found >= 0 ? Found : Layout.FindNearestDisplay( CenterX, CenterY );
}

float32 CWindowsDisplayInfoSubsystem::GetWindowDpiScale() const
{
	const HWND Handle = Window != nullptr ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr;
	if ( Handle == nullptr || !IsWindow( Handle ) )
	{
		return 1.0f;
	}
	const UINT Dpi = GetDpiForWindow( Handle );
	return Dpi > 0 ? static_cast<float32>( Dpi ) / 96.0f : 1.0f;
}
