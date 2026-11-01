// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Capture/CaptureTypes.h"
#include "Library/Math/Color.h"

class CGameObject;
class IDrawExecutor;
class ISceneManager;

//=============================================================================
// NOffscreenShot
//   画面とは別の大きな描画先へ、シーンをもう一度描いて撮る（高解像度・被写体だけ）。
//   描き直しは、描画の実行先の窓口（IDrawExecutor::BeginOffscreenCapture）と、シーンの描き直し（ISceneManager::RedrawScenesForCapture）だけを使う。
//   被写体だけのときは、黒と白の背景で 2 回描いて、NAlphaMatte で背景を透明にする。
//=============================================================================
namespace NOffscreenShot
{
	// 撮る大きさと、2D の倍率を決める（画面の大きさ ScreenWidth x ScreenHeight。決められなければ false）
	bool ResolveSize( const FHighResShotOptions& Options, int32 ScreenWidth, int32 ScreenHeight, int32& OutWidth, int32& OutHeight, float32& OutScale );

	// 撮って、画素（Width x Height。被写体だけなら透明度つき）を Out へ。Subject が nullptr なら画面と同じ絵（不透明）
	bool Render( IDrawExecutor& Executor, ISceneManager& Scenes, int32 Width, int32 Height, float32 Scale, const TCallable<bool( const CGameObject& )>* Subject, TArray<FColor32>& Out );
}
