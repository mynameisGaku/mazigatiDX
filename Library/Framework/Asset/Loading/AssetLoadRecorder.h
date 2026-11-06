// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

#include "Library/Core/Profiler/ProfilerScope.h"

class CAsset;

// 起動してからの、読み込みの累計
struct FAssetLoadCounters
{
	int32 NumLoads = 0;            // 実際に読み込んだ回数
	int32 NumCacheHits = 0;        // 読み込み済みのものを共有した回数
	int32 NumLoadFailures = 0;     // 失敗した回数
	float64 TotalLoadMs = 0.0;     // 読み込みにかかった時間の合計
	float32 MaxLoadMs = 0.0f;      // 1 回で一番長かった時間
	FString MaxLoadPath;           // その読み込みのパス
};

//=============================================================================
// CAssetLoadRecorder
//   読み込みの結果を記録する人。読み込みにかかった時間・ファイルの大きさをアセットに書き込み、累計（回数・共有・失敗・時間）を数える。
//   各ローダーが、読み込みのたびに呼ぶ。集計の表示（CAssetSubsystem::GetStats）は、この累計を読む。
//=============================================================================
class CAssetLoadRecorder
{
public:
	// 読み込みの結果を記録する（読み込みにかかった時間・ファイルの大きさ・累計）。FullPath が空なら、ファイルの大きさは 0
	void RecordLoad( CAsset& Asset, const FString& FullPath, int64 StartTick );

	void RecordCacheHit()
	{
		++Counters.NumCacheHits;
	}

	void RecordFailure()
	{
		++Counters.NumLoadFailures;
	}

	const FAssetLoadCounters& GetCounters() const
	{
		return Counters;
	}

	// 読み込みの計測の目印（プロファイラーのサンプルに、パスを表示名として付ける）。PROFILE_SCOPE_LAZY( 名前, CAssetLoadRecorder::MakeProfileScope( "LoadTexture", FullPath ) ) のように使う
	static FProfileScope MakeProfileScope( const char* Name, const FString& Path );

	// 前に呼んでから、読み込みがあったか。あったら true を返して、印を下ろす（集計の更新を早めるために使う）
	bool ConsumeLoadHappened();

private:
	FAssetLoadCounters Counters;
	bool bLoadHappened = false;
};
