// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NCrashReporter
//   クラッシュ（落ちた・止まった）の原因を、あとから調べられるように、報告を書く（Debug・Release のどちらでも有効）。
//   起動の最初に 1 回 Install する（NGameMain が呼ぶ）。落ちると、<保存先>/Crash_<日時>_<プロセス番号>.txt と .dmp を書く。
//
//   .txt（人が読む）: 時刻・理由・例外の種類と場所（モジュール + 位置・読み書きしたアドレス）・呼び出し履歴（シンボルがあれば関数名と行）・
//                     OS とメモリ・起動引数・読み込んでいるモジュール・直前のログ（ログファイルの末尾）
//   .dmp（Visual Studio / WinDbg で開く）: スレッドの状態とスタックのミニダンプ
//
//   落ちたときの流れ: 報告（.txt と .dmp）を書く → 入力の解放（マウスの拘束・カーソルの非表示を戻す。Platform が必ず行う）→ ログを書き出す →
//   エンジンの後始末（AddEngineCrashHandler で登録された順。音を止める・プロファイラーの記録を書き出す、など）→ ユーザーのコールバック（SetCrashCallback）。
//   後始末とコールバックは、どれも、中で落ちても握りつぶして（ログに出す）次へ進む。
//   報告を書くきっかけ: 処理されない例外（アクセス違反・0 除算・スタックオーバーフローなど）/ abort()（標準の assert・std::terminate・CHECK / Fatal ログも）/
//   CRT の不正な引数。書き終えたあとは、元の動き（デバッガーが付く・Windows のエラー報告）に任せる。
//   報告を書く作業は、別のスレッドで行う（スタックを使い切った落ち方でも書けるように）。同じ落ち方で、2 つ目の報告は書かない。
//
//   Release でも、呼び出し履歴の関数名・行番号が出るには、exe と同じフォルダーに .pdb が必要（無ければ、モジュール + 位置だけ。位置は、.pdb から引ける）。
//   起動引数 --crash-test=access / divide / stackoverflow / abort / check / ensure で、わざと落とせる（報告の確認用）。
//=============================================================================
// クラッシュしたときに、コールバックへ渡す情報
struct FCrashInfo
{
	FString Reason;                       // きっかけ（"Unhandled exception" / "abort()" / "Abort" / "Invalid CRT parameter"）
	FString ReportPath;                   // 書いた報告（.txt）。書けなかったときは空
	FString DumpPath;                     // 書いたダンプ（.dmp）。書けなかったときは空
	bool bHasException = false;           // 例外が原因か（abort・CHECK などは false）
	uint32 ExceptionCode = 0;             // 例外の種類（0xC0000005 など。bHasException が true のとき）
};

// クラッシュしたときに呼ばれる関数。ユーザーが NCrashReporter::SetCrashCallback で設定する
using FCrashCallback = TCallable<void( const FCrashInfo& )>;

// エンジンが、クラッシュのときにする後始末（関数ポインター: 落ちたあとなので、ヒープを確保する入れ物は使わない）。NCrashReporter::AddEngineCrashHandler で登録する
using FEngineCrashHandler = void ( * )( const FCrashInfo& Info );

namespace NCrashReporter
{
	// 報告の保存先を決め、落ち方の受け口を登録する（呼び出し履歴の関数は、NAssert にも登録する）。同じ保存先なら、2 回呼んでもよい
	void Install( const FString& ReportDirectory );

	// エンジンの後始末を登録する（NGameMain が、標準のものを登録する）。報告を書いたあと、ユーザーのコールバックの前に、登録した順に、1 つずつ呼ばれる。
	//   - Name は、同じものがあれば、置き換える（ログと検査に使う。文字列リテラルなど、プロセスの終わりまで有効なもの）。最大 16 個。入りきらなければ false
	//   - 落ちた直後の状態で動く。ふつうの機能に触れてよいかは、その後始末ごとに考える（落ちたスレッドで動く。ゲームスレッドで落ちたなら、DxLib にも触れる）
	//   - 中で落ちても、握りつぶして（ログに出して）次の後始末へ進む。長く止まる後始末は、プロセスの終了を止める
	bool AddEngineCrashHandler( const char* Name, FEngineCrashHandler Handler );
	bool RemoveEngineCrashHandler( const char* Name );
	bool HasEngineCrashHandler( const char* Name );
	int32 GetNumEngineCrashHandlers();

	// クラッシュしたときに呼ばれる関数を設定する（空の FCrashCallback / nullptr で解除）。フレームワークは、何も設定しない（設定口だけ）。
	//   - 呼ばれるのは、報告（.txt と .dmp）とエンジンの後始末（AddEngineCrashHandler）が終わったあと、プロセスが終わる前に、1 回だけ。落ちたスレッドで、同期的に呼ばれる（呼んでいる間は、プロセスは終わらない）
	//   - 落ちた直後の、壊れているかもしれない状態で動く。短く終わるようにし、エンジンやアセットなど、ふつうの機能には触らない（ヒープの確保・ロックは避ける）。
	//     長く止まるコールバックは、プロセスの終了を止める。中でさらに落ちたときは、握りつぶして（ログに出して）進む
	//   - 呼ばれない落ち方: タスクマネージャーからの終了・電源断など、プロセスに通知が来ないもの
	//   - 呼ぶのは、処理されない例外・abort()（CHECK・Fatal ログ・std::terminate を含む）・CRT の不正な引数。WriteReport の直接の呼び出しでは呼ばれない
	void SetCrashCallback( const FCrashCallback& Callback );

	// 落ち方の受け口がすること（報告を書き、ログに残し、設定されたコールバックを呼ぶ）を、1 回、そのまま行う。落ち方の受け口は、これを、プロセスで 1 回だけ呼ぶ。
	// 検査・独自の例外処理から呼ぶためにも公開している（呼んでも、プロセスは終わらない）。ExceptionPointers は WriteReport と同じ。戻り値は、コールバックに渡した情報
	FCrashInfo HandleCrash( const char* Reason, void* ExceptionPointers );

	// 報告に末尾を載せる、ログファイル（絶対パス）。空なら載せない
	void SetLogFilePath( const FString& LogFilePath );

	bool IsInstalled();

	// いまの報告の保存先（Install で決めたもの。検査で、別の場所に変えたあと、戻すために使う）
	FString GetReportDirectory();

	// 報告（.txt と .dmp）を書く。Reason は、報告の先頭に書く理由。ExceptionPointers は EXCEPTION_POINTERS*（例外の中の、__except の GetExceptionInformation()）で、
	// nullptr なら、呼んだ所の文脈。書いた .txt のパス（失敗したら空）。Install していなければ、何もしない
	FString WriteReport( const char* Reason, void* ExceptionPointers );

	// いまのスレッドの呼び出し履歴（1 行に 1 段）。SkipFrames 段は飛ばす。シンボルが無ければ、モジュール + 位置
	FString CaptureCallStack( int32 SkipFrames, int32 MaxDepth );

	// このプロセスで、いちばん最近に書いた報告の .txt のパス（まだ書いていなければ空）
	FString GetLastReportPath();

	// わざと落とす（--crash-test の中身。Kind: access / divide / stackoverflow / abort / check / ensure）。知らない Kind なら false（何も起きない）。ensure は、落ちずに戻る
	bool TriggerTestCrash( const FString& Kind );
}
