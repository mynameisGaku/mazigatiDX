#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/WindowSettings.h"

//=============================================================================
// IPlatform
//   ウィンドウ・フレーム進行・時間を提供するプラットフォーム層。
//   エンジンとゲームはこのインターフェースだけを知り、DxLib などの実装には依存しない。
//
//   IPlatform* Platform = GetAppSubsystem<IPlatform>();
//   Platform->ToggleFullscreen();
//=============================================================================
class IPlatform
{
public:
	virtual ~IPlatform() = default;

	//-------------------------------------------------------------------------
	// フレーム（CEngine が呼ぶ）
	//-------------------------------------------------------------------------
	virtual bool IsInitialized() const = 0;
	// フレーム開始。終了すべきなら false
	virtual bool BeginFrame() = 0;
	virtual void EndFrame() = 0;

	virtual void RequestExit() = 0;
	virtual bool IsExitRequested() const = 0;

	//-------------------------------------------------------------------------
	// 時間
	//-------------------------------------------------------------------------
	virtual float32 GetDeltaTime() const = 0;   // 前フレームからの経過秒
	virtual float64 GetTime() const = 0;        // 起動からの経過秒
	virtual uint64  GetFrameCount() const = 0;
	virtual float32 GetFPS() const = 0;

	//-------------------------------------------------------------------------
	// ウィンドウ
	//-------------------------------------------------------------------------
	virtual int32 GetScreenWidth() const = 0;
	virtual int32 GetScreenHeight() const = 0;

	virtual EWindowMode GetWindowMode() const = 0;
	virtual void SetWindowMode( EWindowMode Mode ) = 0;
	// Windowed <-> 直前のフルスクリーン系モード
	virtual void ToggleFullscreen() = 0;

	virtual const FString& GetWindowTitle() const = 0;
	virtual void SetWindowTitle( const FString& Title ) = 0;

	//-------------------------------------------------------------------------
	// マウスカーソル（表示するかどうかはゲーム側が決める。フレームワークは勝手に変えない）
	//-------------------------------------------------------------------------
	virtual bool IsMouseCursorVisible() const = 0;
	virtual void SetMouseCursorVisible( bool bVisible ) = 0;

	//-------------------------------------------------------------------------
	// スクリーンショット（実装しなくてもよい）
	//-------------------------------------------------------------------------
	// 今のフレームの終わり（DelayFrames が 1 以上なら、そのフレーム数あとのフレームの終わり）に、画面（ImGui を含む）を PNG で保存するよう頼む。
	// Path のフォルダは作ってある前提。頼めなければ false
	virtual bool RequestScreenshot( const FString& Path, int32 DelayFrames = 0 ) { (void)Path; (void)DelayFrames; return false; }
};
