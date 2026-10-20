#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Profiler/ProfilerTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IProfiler
//   処理時間の計測。普段は止めておき、ボタン 1 つ（SetEnabled）で記録を始める。
//   止まっている間の計測コードのコストは「ポインタが null か調べるだけ」。
//
//   計測は PROFILE_SCOPE などのマクロ (Library/Core/Profiler/ProfilerScope.h) から行い、
//   このインターフェースを直接呼ぶのは「操作する側」と「結果を見る側」だけ。
//
//   使い方:
//     IProfiler* Profiler = GetAppSubsystem<IProfiler>();
//     Profiler->SetEnabled( true );          // 記録開始（次のフレームの頭から）
//     Profiler->CaptureFrames( 120 );        // 120 フレーム記録して自動で止める
//     Profiler->SetEnabled( false );         // 止める（記録済みのデータは残る）
//
//   スレッド: どのスレッドからでも計測できる（ジョブシステムのワーカーなど）。フレームの区切りはメインスレッド（初期化したスレッド）の
//   BeginFrame で決まり、メインスレッドの分は GetFrame().Samples、それ以外は GetFrame().Threads に入る。
//   ワーカーは起動時に NProfiler::SetThreadName( "Worker 1" ) で名前を付けておくと、タイムラインのスレッドの行に出る。
//   注意: スレッドの寿命の間ずっと開いたままの計測（PROFILE_SCOPE( "WorkerLoop" ) をループ全体に掛けるなど）は、
//   その外側の木が完成しないので記録が溜まり続ける。ジョブ 1 つごとに計測すること
//=============================================================================
class IProfiler
{
public:
	virtual ~IProfiler() = default;

	//-------------------------------------------------------------------------
	// 操作
	//-------------------------------------------------------------------------
	// 記録のオン / オフ。フレームの途中では切り替わらず、次のフレームの頭で反映される。
	// オフ → オンのときは記録済みのデータを消して新しく始める
	virtual void SetEnabled( bool bEnabled ) = 0;
	virtual bool IsEnabled() const = 0;

	// 今のフレームを記録中か
	virtual bool IsRecording() const = 0;

	// NumFrames フレーム記録して自動で止める（記録済みのデータは消える）
	virtual void CaptureFrames( int32 NumFrames ) = 0;
	// CaptureFrames の残りフレーム数（0 なら実行中ではない）
	virtual int32 GetRemainingCaptureFrames() const = 0;

	// 記録済みのデータをすべて消す
	virtual void Clear() = 0;

	// 記録した履歴は、既定では捨てずにすべて残す。上限を決めたときだけ、超えた分を古いものから捨てる（0 = 無制限）
	virtual void SetMaxFrames( int32 NumFrames ) = 0;
	virtual int32 GetMaxFrames() const = 0;
	// 保存しているサンプルの合計の上限（メモリの目安: 1 サンプル 約 56 バイト）
	virtual void SetMaxStoredSamples( int32 NumSamples ) = 0;
	virtual int32 GetMaxStoredSamples() const = 0;

	// 1 フレームあたりのサンプル数の上限（1 フレームが異常に多いサンプルでメモリを食わないための安全弁）
	virtual void SetMaxSamplesPerFrame( int32 NumSamples ) = 0;
	virtual int32 GetMaxSamplesPerFrame() const = 0;

	// 保存しているサンプルの合計と、データ全体の大きさ（バイト）
	virtual int64 GetNumStoredSamples() const = 0;
	virtual int64 GetStoredBytes() const = 0;

	// 履歴を捨てない代わりに、記録したデータが使うメモリの上限を決める（MB。0 = 無制限）。
	// 超えたら記録を自動で止める（それまでのデータは残る）。止まったあとは IsMemoryLimitReached() が true になる
	virtual void SetMemoryLimitMB( int32 Megabytes ) = 0;
	virtual int32 GetMemoryLimitMB() const = 0;
	virtual bool IsMemoryLimitReached() const = 0;

	//-------------------------------------------------------------------------
	// フレーム（CEngine がフレームの頭で呼ぶ。前のフレームの記録をここで確定する）
	//-------------------------------------------------------------------------
	virtual void BeginFrame() = 0;

	//-------------------------------------------------------------------------
	// 文字列（サンプルは文字列そのものではなく ID を持つ）
	//-------------------------------------------------------------------------
	// 内容が同じなら同じ ID を返す。空文字列は 0
	virtual uint32 InternString( const char* Text ) = 0;
	// ポインタが同じなら同じ ID を返す（文字列リテラルや typeid().name() など、寿命が続くポインタ専用で速い）
	virtual uint32 InternLiteral( const char* Literal ) = 0;
	virtual const FString& GetString( uint32 Id ) const = 0;

	//-------------------------------------------------------------------------
	// サンプルの記録（PROFILE_SCOPE が呼ぶ。直接呼ばない）
	//   戻り値は EndSample に渡す番号（記録されなかったら INDEX_NONE）。Begin と End は同じスレッドから呼ぶ
	//-------------------------------------------------------------------------
	virtual int64 BeginSample( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label ) = 0;
	virtual void EndSample( int64 SampleId ) = 0;

	//-------------------------------------------------------------------------
	// カウンターの記録（PROFILE_COUNTER が呼ぶ。直接呼ばない）
	//   時間ではない値（メモリ使用量・個数など）を、今のフレームに記録する。Name は InternLiteral / InternString の ID。
	//   同じ名前を同じフレームに何度も記録すると、最後の値になる。どのスレッドからでも呼べる
	//-------------------------------------------------------------------------
	virtual void RecordCounter( uint32 Name, float64 Value, EProfileCounterUnit Unit ) = 0;

	//-------------------------------------------------------------------------
	// 記録したデータの参照
	//-------------------------------------------------------------------------
	// 保持しているフレーム数。Index は 0 が一番古く、GetNumFrames() - 1 が一番新しい
	virtual int32 GetNumFrames() const = 0;
	virtual const FProfilerFrame& GetFrame( int32 Index ) const = 0;

	// 計測自体のコスト（1 サンプルあたり ナノ秒）。結果の見積もりに使う
	virtual float64 GetOverheadNsPerSample() const = 0;
};
