// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Platform/WindowSettings.h"

//=============================================================================
// CNullPlatform
//   IPlatform の、DxLib を使わない実装。窓を出さず、画面に描かず、待たずに、決まった時間（1/60 秒）ずつフレームを進める。
//   「DxLib を使わなくても、ゲームのコード・見本が動く」ことを確かめ続けるための実装で、起動の引数 --backend=null で選ぶ
//   （選び方は Library/Platform/BackendSelection.h）。
//
//   game.exe --backend=null --max-frames=60      60 フレーム動かして、終了する（--max-frames を付けなければ、RequestExit まで）
//
//   - 時間は仮想: 1 フレームは、いつも FixedDeltaTime（1/60 秒）。実際の時間は待たない（結果が、機械の速さに左右されない）
//   - ImGui は、窓も描画もなしで、フレームだけ進める（デバッグ表示の部品が、同じ手順で動くため）。imgui.ini は読まず、書かない
//   - 画面の大きさ・ウィンドウモード・題・マウスカーソルの表示は、値を覚えるだけ
//=============================================================================
class CNullPlatform : public CAppSubsystem, public IPlatform
{
public:
	// 1 フレームの長さ（秒）。いつも同じ値
	static constexpr float32 FixedDeltaTime = 1.0f / 60.0f;

	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// IPlatform
	//-------------------------------------------------------------------------
	bool IsInitialized() const override;
	bool BeginFrame() override;
	void EndFrame() override;

	void RequestExit() override;
	bool IsExitRequested() const override;

	float32 GetDeltaTime() const override;
	float64 GetTime() const override;
	uint64 GetFrameCount() const override;
	float32 GetFPS() const override;

	int32 GetScreenWidth() const override;
	int32 GetScreenHeight() const override;

	EWindowMode GetWindowMode() const override;
	void SetWindowMode( EWindowMode Mode ) override;
	void ToggleFullscreen() override;

	const FString& GetWindowTitle() const override;
	void SetWindowTitle( const FString& Title ) override;

	bool IsMouseCursorVisible() const override;
	void SetMouseCursorVisible( bool bVisible ) override;

	//-------------------------------------------------------------------------
	// Null 固有
	//-------------------------------------------------------------------------
	// 動かすフレーム数の上限（--max-frames=N の値。0 は上限なし）
	int32 GetMaxFrames() const;

private:
	// ImGui の文脈を作る（描画の実装は付けない）
	void InitImGui();
	void ShutdownImGui();
	// ImGui が頼んだ文字の画像の作成・更新・破棄に、何もせず「できた」と答える（描画の実装が無くても、次のフレームに進めるため）
	void AnswerImGuiTextureRequests();

	FWindowSettings Settings;
	bool bInitialized = false;
	bool bImGuiActive = false;
	bool bInFrame = false;
	bool bExitRequested = false;
	int32 MaxFrames = 0;
	EWindowMode LastFullscreenMode = EWindowMode::Borderless;

	float32 DeltaTime = 0.0f;
	float64 TotalTime = 0.0;
	uint64 FrameCount = 0;
};
