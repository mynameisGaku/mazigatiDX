// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Framework/Render/DrawContext.h"

namespace NRenderDetail
{
	// FImmediateDraw の基底より先に初期化される、描画コマンドの入れ物。
	struct FDrawBufferStorage
	{
		FDrawBuffer Storage;
	};
}

//=============================================================================
// FImmediateDraw
//   描いたものを、その場で（Flush するか、スコープを抜けるときに）実行する FDrawContext。
//   描画システム（並列に記録して、あとで直列に実行する）の外から、DxLib を直接呼ばずに描くための窓口。
//   シーンの OnDraw、ゲームインスタンスの OnDraw、オブジェクトの Draw() など、メインスレッドの描画で使う。
//
//   void CTitleScene::OnDraw()
//   {
//       FImmediateDraw Draw( GetDrawExecutor() );
//       Draw.Text( 560, 320, "TITLE", FColor32::White );
//       Draw.Box( 500, 380, 780, 384, FColor32::Gray );
//   }   // ここで実行される
//
//   - 使える描画は FDrawContext と同じ（記録のしかたも同じ）。記録した順に実行される。
//   - 実行は 1 回の Flush につき 1 回（描いたものがなければ実行しない）。Flush のあとは、また最初から描ける。
//   - DxLib を呼ぶ実行先は、メインスレッドから呼ぶ必要がある。ワーカーのスレッドでは使わない（そこでは描画システムを使う）。
//   - 実行先が nullptr なら、描いたものは捨てられる（描画先のない確認用の実行など）。
//=============================================================================
class FImmediateDraw : private NRenderDetail::FDrawBufferStorage, public FDrawContext
{
public:
	// InExecutor は描画の実行先（所有しない。FImmediateDraw より長く生きている必要がある）。
	explicit FImmediateDraw( IDrawExecutor* InExecutor ) : FDrawContext( Storage ), Executor( InExecutor ) {}

	// 描いたものが残っていれば、実行する。
	~FImmediateDraw() { Flush(); }

	FImmediateDraw( const FImmediateDraw& ) = delete;
	FImmediateDraw& operator=( const FImmediateDraw& ) = delete;

	// ここまでに描いたものを実行して、空にする。何も描いていなければ、実行先を呼ばない。
	void Flush();

private:
	// 描画の実行先（所有しない）。
	IDrawExecutor* Executor = nullptr;
};
