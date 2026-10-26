#pragma once
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/CompilerMacros.h"
#include <chrono>
#include <fstream>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/LogVerbosity.h"

//=============================================================================
// FLogMessage
//   シンクに渡される 1 件分のログ
//=============================================================================
struct FLogMessage
{
	const char*   CategoryName = "";
	ELogVerbosity Verbosity    = ELogVerbosity::Log;
	FString       Message;      // フォーマット済み本文
	FString       Function;     // "SomeFeature: SomeFunc"
	const char*   FileName     = "";  // "Hoge.cpp"（パスなし）
	int32         Line         = 0;
	std::chrono::system_clock::time_point Time;

	// 出力用の 1 行
	//   [LogHoge]: SomeFeature: SomeFunc: Anything message. | Hoge.cpp | line: 42
	//   Warning / Error / Fatal はカテゴリの後に重要度が入る
	//   [LogHoge]: Warning: SomeFeature: SomeFunc: Anything message. | Hoge.cpp | line: 42
	FString FormattedLine;
};

//=============================================================================
// ILogSink
//   ログの出力先。CLogManager に登録して使う。
//   Write は CLogManager 側で排他されるため、シンク内での排他は不要
//   （シンク独自のデータを別スレッドから読む場合を除く）。
//=============================================================================
class ILogSink
{
public:
	virtual ~ILogSink() = default;

	virtual void Write( const FLogMessage& Message ) = 0;
	virtual void Flush();

	// このシンクに出力する最大 Verbosity
	FORCEINLINE ELogVerbosity GetMaxVerbosity() const { return MaxVerbosity; }
	FORCEINLINE void SetMaxVerbosity( ELogVerbosity InVerbosity ) { MaxVerbosity = InVerbosity; }

private:
	ELogVerbosity MaxVerbosity = ELogVerbosity::All;
};

//=============================================================================
// CConsoleLogSink
//   Visual Studio の出力ウィンドウ (OutputDebugString) と
//   コンソール（存在する場合）に出力する。
//=============================================================================
class CConsoleLogSink : public ILogSink
{
public:
	// bAllocConsole = true でコンソールウィンドウを新規に開く
	explicit CConsoleLogSink( bool bAllocConsole = false );
	~CConsoleLogSink() override;

	void Write( const FLogMessage& Message ) override;

private:
	bool bOwnsConsole = false;
};

//=============================================================================
// CFileLogSink
//   ファイルに UTF-8 で出力する。1 行ごとにファイルへ反映するので、起動中に別のツールで読み続けられ、
//   クラッシュしても直前までのログが残る。
//
//   bExclusiveWrite = true なら、他のプロセスが同じファイルへ書き込めないように開く（読むのは許可）。
//   他のプロセスが既に開いていると失敗する（IsOpen() が false）ので、アプリの複数起動でログが混ざるのを防げる。
//=============================================================================
class CFileLogSink : public ILogSink
{
public:
	explicit CFileLogSink( const FString& FilePath, bool bAppend = false, bool bExclusiveWrite = false );
	~CFileLogSink() override;

	void Write( const FLogMessage& Message ) override;
	void Flush() override;

	FORCEINLINE bool IsOpen() const { return File.is_open(); }

private:
	std::ofstream File;
};

//=============================================================================
// CImGuiLogSink
//   ログを保持し、ImGui ウィンドウとして表示する。
//   毎フレーム ImGui::NewFrame() 後に Draw() を呼ぶこと。
//=============================================================================
class CImGuiLogSink : public ILogSink
{
public:
	explicit CImGuiLogSink( int32 InMaxLines = 2000 );

	void Write( const FLogMessage& Message ) override;

	void Draw( const char* Title = "Output Log", bool* pOpen = nullptr );
	void Clear();

private:
	struct FEntry
	{
		ELogVerbosity Verbosity;
		FString       Text;  // UTF-8
	};

	// 古い順に並べたときの Index 番目（リングバッファ）
	FORCEINLINE const FEntry& GetEntry( int32 Index ) const { return Entries[( Head + Index ) % Entries.Num()]; }

	FMutex         Mutex;
	TArray<FEntry>     Entries;          // MaxLines を上限とするリングバッファ
	int32              Head        = 0;  // 最も古い要素の位置
	int32              MaxLines;
	bool               bAutoScroll     = true;
	int32              FilterVerbosity = static_cast<int32>( ELogVerbosity::All );
	char               FilterText[128] = {};
};
