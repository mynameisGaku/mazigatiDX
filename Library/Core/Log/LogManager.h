#pragma once
#include <sal.h>
#include <cstdarg>
#include <mutex>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"

#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Log/LogSink.h"

//=============================================================================
// CLogManager
//   ログのシングルトン。カテゴリ・シンクを管理し、ログを各シンクへ配信する。
//   スレッドセーフ。
//
//   CLogManager::Get().AddSink( MakeShared<CFileLogSink>( "game.log" ) );
//   CLogManager::Get().AddSink( MakeShared<CImGuiLogSink>() );
//=============================================================================
class CLogManager
{
public:
	static CLogManager& Get();

	//-------------------------------------------------------------------------
	// シンク
	//-------------------------------------------------------------------------
	void AddSink( const TSharedPtr<ILogSink>& Sink );
	void RemoveSink( const TSharedPtr<ILogSink>& Sink );
	void RemoveAllSinks();
	void Flush();

	//-------------------------------------------------------------------------
	// カテゴリ
	//-------------------------------------------------------------------------
	// 名前でカテゴリを検索（見つからなければ nullptr）
	FLogCategory* FindCategory( const char* Name );

	// 名前でカテゴリの Verbosity を変更（見つからなければ false）
	bool SetCategoryVerbosity( const char* Name, ELogVerbosity Verbosity );

	// 登録済みカテゴリ一覧のスナップショット
	TArray<FLogCategory*> GetCategories();

	// すべてのカテゴリの Verbosity を変更する / 既定値（宣言時の値）に戻す
	void SetAllCategoriesVerbosity( ELogVerbosity Verbosity );
	void ResetAllCategoriesToDefault();

	// 起動引数から Verbosity を設定する。戻り値は変更したカテゴリの数
	//   --log-verbose            すべてのカテゴリを Verbose に
	//   --log-veryverbose        すべてのカテゴリを VeryVerbose に（Release では VeryVerbose はコンパイル時に除去されている）
	//   --log=LogScene:Verbose,LogPhysics:VeryVerbose   カテゴリごとに指定
	int32 ApplyCommandLine( const char* CommandLine );

	//-------------------------------------------------------------------------
	// 出力（通常は LOG マクロ経由で呼ぶ）
	//-------------------------------------------------------------------------
	void Log( const FLogCategory& Category, ELogVerbosity Verbosity,
			  const char* File, int32 Line, const char* Function,
			  _Printf_format_string_ const char* Format, ... );

	void LogV( const FLogCategory& Category, ELogVerbosity Verbosity,
			   const char* File, int32 Line, const char* Function,
			   const char* Format, va_list Args );

private:
	friend struct FLogCategory;

	CLogManager() = default;
	~CLogManager() = default;
	CLogManager( const CLogManager& ) = delete;
	CLogManager& operator=( const CLogManager& ) = delete;

	void RegisterCategory( FLogCategory* Category );
	void UnregisterCategory( FLogCategory* Category );

	std::mutex SinkMutex;
	TArray<TSharedPtr<ILogSink>> Sinks;

	std::mutex CategoryMutex;
	TArray<FLogCategory*> Categories;
};
