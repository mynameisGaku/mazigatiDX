// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Thread/CommandThread.h"

// 既存の専用実行列を描画スレッドとして起動し、識別の登録と解除まで所有する。作成スレッドが寿命を管理する。
class CRenderCommandThread
{
  public:
	// 専用スレッドで識別を登録する。既存の別の描画スレッドがあればIsReadyはfalse。
	CRenderCommandThread();
	// 要求元を停止した後に作成スレッドで破棄する。全件実行後、識別を解除して結合する。
	~CRenderCommandThread();
	// 専用スレッドの登録が成功したか返す。
	bool IsReady() const;
	// 値を所有する仕事を描画スレッドへ渡す。Nameは文字列リテラル。未起動・拒否なら無効票。
	FTaskFence Enqueue( TCallable<void()> Task, const char *Name = "Render.Command" );
	// 同じ順序で実行し、取り込んだ借用参照を使い終わるまで必ず待つ。描画スレッド内ではその場で実行する。
	// 未起動・空はfalse。満杯なら先行処理を待って再受付する。借用参照を早期に返さず、GPU完了は別途必要。
	bool Execute( TCallable<void()> Task, const char *Name = "Render.Execute" );
	// 今までのCPU命令の区切りを返す。未起動では無効票。
	FTaskFence GetFence() const;
	// 今までに受け付けたCPU処理を全件待つ。満杯でも追加命令を必要としない。描画スレッド内はfalse。
	bool WaitForCommands() const;
	CRenderCommandThread( const CRenderCommandThread & ) = delete;
	CRenderCommandThread &operator=( const CRenderCommandThread & ) = delete;

  private:
	// 受け付けた仕事が終了するまで待つ。専用実行列は受付済みの仕事を破棄しない。
	static bool WaitAccepted( const FTaskFence &Fence );
	// 作成後から終了まで使う既存の専用実行列。
	CCommandThread Thread;
	// 識別の登録結果。初期化と終了は作成スレッドが直列に担当する。
	bool bReady = false;
};
