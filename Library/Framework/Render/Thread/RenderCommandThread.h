// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
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
	// 確認用: 描画スレッドの仕事量の内訳（働いている時間・同期呼び出しの回数と、呼び出し側が待った時間。待った時間の長い呼び出し元の名前も）を、120 フレームごとにログへ出す。
	// 作成直後（仕事を渡す前）に、所有者が 1 回だけ呼ぶ。
	void EnableTimings();
	// 1 フレームが終わったことを数える（ゲームスレッド）。EnableTimings を呼んでいなければ、何もしない。
	void NotifyFrameEnd();
	CRenderCommandThread( const CRenderCommandThread & ) = delete;
	CRenderCommandThread &operator=( const CRenderCommandThread & ) = delete;

  private:
	// 受け付けた仕事が終了するまで待つ。専用実行列は受付済みの仕事を破棄しない。
	static bool WaitAccepted( const FTaskFence &Fence );
	// EnableTimings のとき、仕事の実行時間を数える包みにする。そうでなければ、そのまま返す。
	TCallable<void()> MakeTimed( TCallable<void()> Task );
	// 同期呼び出し 1 回分（呼び出し元の名前と、呼び出し側が待った時間）を、名前ごとの集計へ足す。
	void AddSyncCaller( const char *Name, int64 WaitTicks );
	// 作成後から終了まで使う既存の専用実行列。
	CCommandThread Thread;
	// 識別の登録結果。初期化と終了は作成スレッドが直列に担当する。
	bool bReady = false;
	// 仕事量の計測（EnableTimings のとき）。値は、描画スレッド・呼び出し側のどちらからも足される。
	bool bTimings = false;
	TAtomic<int64> BusyTicks { 0 }; // 描画スレッドが仕事を実行していた時間
	TAtomic<int64> SyncWaitTicks { 0 }; // 同期呼び出し（Execute）で、呼び出し側が待った時間
	TAtomic<int64> SyncCalls { 0 }; // 同期呼び出しの回数
	TAtomic<int64> Tasks { 0 }; // 実行した仕事の数
	int32 TimingFrames = 0; // 集計中のフレーム数（ゲームスレッドだけが触る）
	// 同期呼び出しの、呼び出し元の名前ごとの集計（EnableTimings のとき。Execute は複数のスレッドから呼ばれうるので、排他する）。
	struct FSyncCaller
	{
		const char *Name = nullptr;
		int64 Calls = 0;
		int64 WaitTicks = 0;
	};
	FMutex CallerMutex;
	TArray<FSyncCaller> Callers;
};
