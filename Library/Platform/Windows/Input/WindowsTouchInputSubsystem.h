// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Input/ITouchInput.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Input/Touch/TouchTracker.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsTouchInputSubsystem
//   画面に触れている指を、Windows の WM_TOUCH から取り込んで、ITouchInput として貸す。描画 API を知らない。
//   座標は、ゲームの描画領域の左上が原点の論理座標（IMouseInput と同じ）。ID は、触れている間、同じ指に同じ値。
//   触れて離すまでが 1 フレームに収まっても、1 フレームは見える（次のフレームで離された指になる）。
//=============================================================================
class CWindowsTouchInputSubsystem : public CAppSubsystem, public ITouchInput, public INativeWindowMessageObserver, public ITickable
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の入力が ITouchInput を持つ）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	void Tick( float32 DeltaTime ) override;

	bool IsTouchAvailable() const override;
	const TArray<FTouchPoint>& GetTouches() const override;
	const TArray<uint32>& GetReleasedTouchIds() const override;

	bool OnNativeWindowMessage( void* Window, uint32 Message, uint64 WParam, int64 LParam, int64& OutResult ) override;

	// 指の動き 1 つを取り込む（WM_TOUCH の読み取りと、検査が使う）。座標は論理座標
	enum class EContact : uint8
	{
		Down,
		Move,
		Up
	};
	void AddContact( uint32 Id, int32 X, int32 Y, EContact Contact );
	// 窓を借りて、WM_TOUCH を受け取れる状態にする（Initialize が呼ぶ）
	void Bind( INativeWindow* InWindow, IPlatform* InPlatform );

private:
	struct FActive
	{
		FTouchPoint Point;
		bool bEnded = false; // 離された（次の Tick で外す）
	};

	INativeWindow* Window = nullptr;
	IPlatform* Platform = nullptr;
	bool bRegistered = false;
	TArray<FActive> Active;
	FTouchTracker Tracker;
};
