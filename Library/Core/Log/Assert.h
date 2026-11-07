// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// CHECK / ENSURE（Debug・Release のどちらでも有効な、「こうなっているはず」の確認）
//
//   CHECK( Index >= 0 );                                   // 偽なら Fatal（続けられない）: 式・場所・呼び出し履歴をログに出して、止める
//   CHECKF( Handle != 0, "no handle for '%s'", *Name );    // 理由（printf 形式）つき
//   if ( !ENSURE( Target != nullptr ) ) { return; }        // 偽でも続けられる: 偽なら Error（場所ごとに最初の 1 回だけ、呼び出し履歴つき）を出して、偽を返す
//   ENSUREF( Count > 0, "Count = %d", Count );
//   DEBUG_CHECK( ExpensiveTest() );                        // Debug だけ CHECK（Release では、式を評価しない）
//
//   使い分け: CHECK = これが偽だと、先へ進むと壊れる（メモリ・状態）。ENSURE = 偽でも、その場を避ければ続けられる（読み込みの失敗など、起こりうる失敗は、Error / Warning のログと戻り値で扱い、ここには使わない）。
//   - 止まるときは、ログ（Fatal）→ クラッシュ報告（Platform の NCrashReporter が、異常終了の前に書く。呼び出し履歴・ダンプ・直前のログ）の順
//   - 標準の assert は使わない（Release で何も起きず、Debug でもログに残らない）
//   - 検査では、NAssert::SetFailureHandler で失敗を受け取れる（止まらない）
//=============================================================================
namespace NAssert
{
	enum class EKind : uint8
	{
		Check,
		Ensure,
	};

	// 呼び出し履歴を文字列にする関数（Platform が登録する。SkipFrames は、呼び出し側から数えて飛ばす段数）
	using FCallStackProvider = FString ( * )( int32 SkipFrames, int32 MaxDepth );
	void SetCallStackProvider( FCallStackProvider Provider );
	// 登録された関数で、いまの呼び出し履歴を作る（登録が無ければ空）
	FString CaptureCallStack( int32 SkipFrames = 0, int32 MaxDepth = 24 );

	// 失敗を受け取る関数（検査用）。true を返すと「受け取った」ことになり、ログも停止もしない（CHECK のあとも、続きが実行される）
	using FFailureHandler = bool ( * )( EKind Kind, const char* Message );
	void SetFailureHandler( FFailureHandler Handler );

	// ENSURE が偽だった回数（場所ごとの 1 回に限らず、全部）
	int64 GetNumEnsureFailures();

	// マクロから呼ばれる。Format は nullptr でもよい（printf 形式）
	void ReportCheckFailed( const char* Expression, const char* File, int32 Line, const char* Function, const char* Format, ... );
	void ReportEnsureFailed( bool& bAlreadyReported, const char* Expression, const char* File, int32 Line, const char* Function, const char* Format, ... );
}

#define CHECK( Expr )                                                                                  \
	do                                                                                                 \
	{                                                                                                  \
		if ( !( Expr ) )                                                                               \
		{                                                                                              \
			NAssert::ReportCheckFailed( #Expr, __FILE__, __LINE__, __FUNCTION__, nullptr );            \
		}                                                                                              \
	} while ( 0 )

#define CHECKF( Expr, Format, ... )                                                                    \
	do                                                                                                 \
	{                                                                                                  \
		if ( !( Expr ) )                                                                               \
		{                                                                                              \
			NAssert::ReportCheckFailed( #Expr, __FILE__, __LINE__, __FUNCTION__, Format, ##__VA_ARGS__ ); \
		}                                                                                              \
	} while ( 0 )

// 偽なら false を返す（真なら true）。場所ごとの static で、最初の 1 回だけログに出す
#define ENSUREF( Expr, Format, ... )                                                                   \
	( ( Expr ) ? true : ( [&]() -> bool                                                                \
	{                                                                                                  \
		static bool bReported = false;                                                                 \
		NAssert::ReportEnsureFailed( bReported, #Expr, __FILE__, __LINE__, __FUNCTION__, Format, ##__VA_ARGS__ ); \
		return false;                                                                                  \
	}() ) )

#define ENSURE( Expr ) ENSUREF( Expr, nullptr )

#ifdef _DEBUG
#define DEBUG_CHECK( Expr ) CHECK( Expr )
#else
#define DEBUG_CHECK( Expr ) do { } while ( 0 )
#endif
