#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <atomic>

#include "Library/Core/Log/LogVerbosity.h"

//=============================================================================
// FLogCategory
//   ログカテゴリ。名前と実行時の Verbosity を持つ。
//   直接生成せず DECLARE / DEFINE マクロを使うこと。
//
//   // MySystem.h
//   DECLARE_LOG_CATEGORY_EXTERN( LogMySystem, Verbose )
//   // MySystem.cpp
//   DEFINE_LOG_CATEGORY( LogMySystem )
//
//   // .cpp 内だけで使う場合
//   DEFINE_LOG_CATEGORY_STATIC( LogMyLocal, Log )
//=============================================================================
struct FLogCategory
{
public:
	FLogCategory( const char* InName, ELogVerbosity InDefaultVerbosity );
	~FLogCategory();

	FLogCategory( const FLogCategory& ) = delete;
	FLogCategory& operator=( const FLogCategory& ) = delete;

	FORCEINLINE const char* GetName() const { return Name; }

	FORCEINLINE ELogVerbosity GetVerbosity() const { return Verbosity.load( std::memory_order_relaxed ); }
	FORCEINLINE void SetVerbosity( ELogVerbosity InVerbosity ) { Verbosity.store( InVerbosity, std::memory_order_relaxed ); }

	FORCEINLINE ELogVerbosity GetDefaultVerbosity() const { return DefaultVerbosity; }

	// このカテゴリで指定 Verbosity のログが抑制されるか（Fatal は抑制されない）
	FORCEINLINE bool IsSuppressed( ELogVerbosity InVerbosity ) const
	{
		return InVerbosity != ELogVerbosity::Fatal && InVerbosity > GetVerbosity();
	}

private:
	const char* Name;
	ELogVerbosity DefaultVerbosity;
	std::atomic<ELogVerbosity> Verbosity;
};

//-----------------------------------------------------------------------------
// ヘッダーで宣言（他の翻訳単位から使えるようにする）
//-----------------------------------------------------------------------------
#define DECLARE_LOG_CATEGORY_EXTERN( CategoryName, DefaultVerbosity )                    \
	extern struct FLogCategory##CategoryName : public FLogCategory                       \
	{                                                                                    \
		FLogCategory##CategoryName()                                                     \
			: FLogCategory( #CategoryName, ELogVerbosity::DefaultVerbosity ) {}          \
	} CategoryName;

//-----------------------------------------------------------------------------
// .cpp で実体を定義（DECLARE_LOG_CATEGORY_EXTERN と対で使う）
//-----------------------------------------------------------------------------
#define DEFINE_LOG_CATEGORY( CategoryName ) \
	FLogCategory##CategoryName CategoryName;

//-----------------------------------------------------------------------------
// .cpp 内だけで使うカテゴリを宣言・定義
//-----------------------------------------------------------------------------
#define DEFINE_LOG_CATEGORY_STATIC( CategoryName, DefaultVerbosity )                     \
	static struct FLogCategory##CategoryName : public FLogCategory                       \
	{                                                                                    \
		FLogCategory##CategoryName()                                                     \
			: FLogCategory( #CategoryName, ELogVerbosity::DefaultVerbosity ) {}          \
	} CategoryName;
