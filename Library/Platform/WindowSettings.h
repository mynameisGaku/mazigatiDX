#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <initializer_list>

#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// EWindowMode
//=============================================================================
enum class EWindowMode : uint8
{
	Windowed,    // 通常のウィンドウ
	Borderless,  // 枠なしで画面全体に表示（仮想フルスクリーン。Alt+Tab が速い）
	Fullscreen,  // フルスクリーン（デスクトップ解像度のまま拡大表示）
};

FORCEINLINE const char* WindowModeToString( EWindowMode Mode )
{
	switch ( Mode )
	{
	case EWindowMode::Windowed:   return "Windowed";
	case EWindowMode::Borderless: return "Borderless";
	case EWindowMode::Fullscreen: return "Fullscreen";
	}
	return "Windowed";
}

// "Windowed" / "Borderless" / "Fullscreen"（大文字小文字は区別しない）
FORCEINLINE TOptional<EWindowMode> WindowModeFromString( const FString& Str )
{
	const FString Trimmed = Str.TrimStartAndEnd();
	for ( EWindowMode Mode : { EWindowMode::Windowed, EWindowMode::Borderless, EWindowMode::Fullscreen } )
	{
		if ( Trimmed.Equals( WindowModeToString( Mode ), ESearchCase::IgnoreCase ) ) return Mode;
	}
	return {};
}

//=============================================================================
// EScalingFilter（画面を拡大表示するときの補間）
//=============================================================================
enum class EScalingFilter : uint8
{
	Bilinear,  // なめらか
	Nearest,   // くっきり（ドット絵向け）
};

//=============================================================================
// FWindowSettings
//   CDxLibBackend::Init / Run に渡す起動設定。
//   ※ Width / Height はゲーム画面（描画先）の解像度。ウィンドウの大きさは Width * WindowExtendRate
//=============================================================================
struct FWindowSettings
{
	//-------------------------------------------------------------------------
	// ウィンドウ
	//-------------------------------------------------------------------------
	FString     Title            = "Game";
	int32       Width            = 1280;
	int32       Height           = 720;
	int32       ColorBitDepth    = 32;
	EWindowMode WindowMode       = EWindowMode::Windowed;
	float64     WindowExtendRate = 1.0;     // Windowed 時のウィンドウ倍率
	int32       PositionX        = -1;      // Windowed 時の位置（-1 で中央）
	int32       PositionY        = -1;
	bool        bResizable       = false;   // ウィンドウ端をつかんでサイズ変更できるか
	bool        bShowMouseCursor = true;    // 起動時のマウスカーソル表示（実行中は IPlatform::SetMouseCursorVisible）
	EScalingFilter ScalingFilter = EScalingFilter::Bilinear;

	//-------------------------------------------------------------------------
	// 動作
	//-------------------------------------------------------------------------
	bool  bVSync         = true;
	int32 MaxFPS         = 0;       // 0 で無制限（bVSync 時はモニタに同期）
	bool  bAlwaysRun     = true;    // 非アクティブ時も動かすか
	bool  bAllowMultipleInstances = true;
	bool  bOutputDxLibLog = false;  // DxLib の Log.txt を出力するか

	//-------------------------------------------------------------------------
	// 3D
	//-------------------------------------------------------------------------
	int32   ZBufferBitDepth = 16;
	bool    bUseZBuffer3D   = true;
	float32 CameraNear      = 0.1f;
	float32 CameraFar       = 10000.0f;

	//-------------------------------------------------------------------------
	// ImGui
	//-------------------------------------------------------------------------
	bool    bEnableImGui       = true;
	bool    bImGuiDocking      = true;
	bool    bImGuiViewports    = true;   // ImGui ウィンドウをメインウィンドウの外に出せる
	// 外部ウィンドウの 1 回の表示 (Present) は、垂直同期 1 回分ほど待たされる環境がある。
	// 外部ウィンドウが 4 枚あれば毎フレーム 4 回待たされ、ゲーム本体の FPS が 1/5 近くまで落ちるので、更新頻度を抑える。
	int32   ImGuiViewportMaxFPS = 30;               // 外部ウィンドウ 1 枚あたりの最大更新回数 / 秒（0 で制限なし = 毎フレーム）
	int32   ImGuiViewportMaxPresentsPerFrame = 1;   // 1 フレームに表示する外部ウィンドウの最大数（0 で制限なし）
	FString ImGuiFontPath      = "C:\\Windows\\Fonts\\meiryo.ttc";
	float32 ImGuiFontSize      = 18.0f;
	bool    bImGuiSaveIni      = true;   // imgui.ini にレイアウトを保存するか
};
