#pragma once
#include <memory>
#include <vector>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// FProfilerWorkerBuffer
//   メインスレッド以外のスレッド 1 つ分の、記録中のサンプル。スレッドごとに 1 つ（そのスレッドの最初の計測で作る）。
//   ほかのスレッドと共有しないので、計測中のロックはこのバッファの Mutex だけ（メインスレッドがフレームの終わりに
//   完成した木を取り出すときに、少しの間ぶつかる）
//=============================================================================
struct FProfilerWorkerBuffer
{
	FMutex Mutex;
	uint32 ThreadId = 0;
	uint32 NameId = 0;
	const char* NamePtr = nullptr;
	TArray<FProfileSample> Samples;   // 完成した木から順に、フレームへ移して取り除く
	int64 BaseId = 0;                 // Samples[0] の通し番号（取り除いても、開いているサンプルの番号は変わらない）
	int32 CurrentTop = INDEX_NONE;    // 開いているサンプルのうち一番内側（Samples のインデックス）
};

//=============================================================================
// CProfilerSubsystem
//   IProfiler の実装。アプリ全体で 1 つ、自動で生成される（常に存在するが、オンにするまで何も記録しない）。
//   結果は時間順にすべて保持し（既定では捨てない。メモリの上限に達したら記録を止める）、画面表示（NDebugUI::DrawProfilerWindow）や
//   集計（ProfilerAnalyzer）、Chrome トレースへの書き出し（ProfilerExport）から読む。
//
//   GetAppSubsystem<IProfiler>() で取得する。
//
//   スレッド: メインスレッドは、ロックなしの速い経路で記録する。ほかのスレッドは、スレッドごとのバッファに記録し、
//   メインスレッドのフレームの終わりに、完成した木を「木の根が始まった時刻のフレーム」へ移す。
//   文字列の表は全スレッドで共有し（登録は排他、引く方はロックなし）、スレッドごとの小さな控えで、ほとんどの呼び出しはロックしない。
//=============================================================================
class CProfilerSubsystem : public CAppSubsystem, public IProfiler
{
public:
	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// IProfiler
	//-------------------------------------------------------------------------
	void SetEnabled( bool bEnabled ) override;
	FORCEINLINE bool IsEnabled() const override { return bEnabled; }
	FORCEINLINE bool IsRecording() const override { return bRecording; }

	void CaptureFrames( int32 NumFrames ) override;
	FORCEINLINE int32 GetRemainingCaptureFrames() const override { return RemainingCaptureFrames; }

	void Clear() override;

	void SetMaxFrames( int32 NumFrames ) override;
	FORCEINLINE int32 GetMaxFrames() const override { return MaxFrames; }
	void SetMaxSamplesPerFrame( int32 NumSamples ) override;
	FORCEINLINE int32 GetMaxSamplesPerFrame() const override { return MaxSamplesPerFrame; }

	void SetMaxStoredSamples( int32 NumSamples ) override;
	FORCEINLINE int32 GetMaxStoredSamples() const override { return MaxStoredSamples; }
	FORCEINLINE int64 GetNumStoredSamples() const override { return StoredSamples; }
	int64 GetStoredBytes() const override;

	void SetMemoryLimitMB( int32 Megabytes ) override;
	FORCEINLINE int32 GetMemoryLimitMB() const override { return MemoryLimitMB; }
	FORCEINLINE bool IsMemoryLimitReached() const override { return bMemoryLimitReached; }

	void BeginFrame() override;

	uint32 InternString( const char* Text ) override;
	uint32 InternLiteral( const char* Literal ) override;
	const FString& GetString( uint32 Id ) const override;

	int64 BeginSample( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label ) override;
	void EndSample( int64 SampleId ) override;
	void RecordCounter( uint32 Name, float64 Value, EProfileCounterUnit Unit ) override;

	FORCEINLINE int32 GetNumFrames() const override { return Frames.Num() - FirstFrameIndex; }
	const FProfilerFrame& GetFrame( int32 Index ) const override;

	FORCEINLINE float64 GetOverheadNsPerSample() const override { return OverheadNsPerSample; }

private:
	void StartFrame();
	// 記録中のフレームを確定して保存する
	void FinishFrame();
	// 空のサンプルを大量に記録して、1 サンプルあたりのコストを測る
	void MeasureOverhead();
	// 記録が止まったときに、全体の平均と処理時間の上位をログへ出す（画面を開かなくても概要が分かる）
	void LogSummary() const;
	// 一番古いフレームを捨てる（フレーム数 / サンプル数の上限を決めているときだけ使う）
	void DropOldestFrame();
	// 決めてある上限を超えている分を、古いフレームから捨てる
	void EnforceLimits();

	// メインスレッド以外のスレッドの記録
	FProfilerWorkerBuffer* GetWorkerBuffer();
	int64 BeginWorkerSample( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label );
	void EndWorkerSample( int64 SampleId );
	// 別スレッドで完成した木を、開始時刻のフレームへ移す（メインスレッドのフレームの終わりに呼ぶ）
	void CollectWorkerSamples();
	// Samples は木 1 つ分（根から始まる Count 個）。Parent は取り出した配列での位置なので、TreeStart（その配列での木の先頭の位置）を引いて使う
	void AddWorkerTree( const FProfilerWorkerBuffer& Buffer, const FProfileSample* Samples, int32 Count, int32 TreeStart );
	void ClearWorkerBuffers();

	uint32 InternLocked( const FString& Key );

private:
	bool bEnabled = false;
	bool bRecording = false;
	bool bClearOnStart = false;   // オフ -> オンの切り替えで記録を消す
	int32 RemainingCaptureFrames = 0;
	int32 MaxSamplesPerFrame = 16384;
	int32 MaxFrames = 0;                // 0 = 無制限
	int32 MaxStoredSamples = 0;         // 0 = 無制限
	int32 MemoryLimitMB = 4096;         // 0 = 無制限。超えたら記録を止める（データは捨てない）
	bool bMemoryLimitReached = false;
	int64 StoredSamples = 0;            // 保存しているサンプルの合計（別スレッドの分を含む）
	int64 StoredCounters = 0;           // 保存しているカウンターの合計
	FMutex CounterMutex;            // Current.Counters を守る（どのスレッドからも記録できる）
	uint32 RecordingThreadId = 0;
	uint64 NextFrameNumber = 1;
	float64 OverheadNsPerSample = 0.0;

	FProfilerFrame Current;
	int32 CurrentTop = INDEX_NONE;   // 開いているサンプルのうち一番内側

	// 確定したフレーム（時間順）。Frames[FirstFrameIndex] が一番古い。それより前は捨てたフレーム（上限を決めたときだけ増える）
	TArray<FProfilerFrame> Frames;
	int32 FirstFrameIndex = 0;

	// 別スレッドの記録
	FMutex WorkerRegistryMutex;
	std::vector<std::unique_ptr<FProfilerWorkerBuffer>> WorkerBuffers;

	// 文字列の表（ID = 番号。0 は空文字列）。別スレッドからも引くので、一度入れた文字列は動かさない（ブロックごとに確保する）
	static constexpr int32 StringBlockSize = 1024;
	static constexpr int32 MaxStringBlocks = 4096;
	TAtomic<FString*> StringBlocks[MaxStringBlocks] = {};
	TAtomic<uint32> NumStrings{ 0 };
	FMutex StringMutex;                  // 登録（と、内容 / ポインタから ID を探す表）を守る
	TMap<FString, uint32> StringIds;
	TMap<const char*, uint32> LiteralIds;
};
