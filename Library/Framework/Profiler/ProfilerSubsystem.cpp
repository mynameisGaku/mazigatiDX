#include "ProfilerSubsystem.h"


#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"

DEFINE_LOG_CATEGORY_STATIC( LogProfiler, Log )

REGISTER_SUBSYSTEM( CProfilerSubsystem )

namespace
{
	constexpr int32 MinMaxSamples = 256;
	constexpr int32 SummaryMaxFrames = 5000;   // 記録を止めたときのログの要約は、直近のこのフレーム数まで（長く記録したときに止まらないように）
	constexpr int64 BytesPerMegabyte = 1024 * 1024;
	constexpr int32 MaxWorkerBufferSamples = 262144;   // 1 スレッドの記録中のサンプルの上限（開きっぱなしの計測で無限に溜まらないように）

	// このスレッドのバッファ（スレッドごと。どのプロファイラーのものかも覚えておく）
	thread_local FProfilerWorkerBuffer* GThreadBuffer = nullptr;
	thread_local const void* GThreadBufferOwner = nullptr;

	// typeid().name() の "class " / "struct " を取り除く
	const char* SkipTypePrefix( const char* Text )
	{
		if ( NCString::CompareN( Text, "class ", 6 ) == 0 ) return Text + 6;
		if ( NCString::CompareN( Text, "struct ", 7 ) == 0 ) return Text + 7;
		return Text;
	}
}

#pragma region Lifecycle

void CProfilerSubsystem::Initialize( FSubsystemCollection& Collection )
{
	RecordingThreadId = NPlatformTime::GetCurrentThreadId();

	// ID 0 = 空文字列
	StringBlocks[0].Store( new FString[StringBlockSize] );
	NumStrings.Store( 1 );

	MeasureOverhead();
	LOG( LogProfiler, Log, "Ready (history: kept, memory limit: %d MB, up to %d samples/frame, overhead: ~%.0f ns/sample). Off until SetEnabled( true )",
		 MemoryLimitMB, MaxSamplesPerFrame, OverheadNsPerSample );
}

void CProfilerSubsystem::Deinitialize()
{
	if ( NProfiler::GRecorder == this ) NProfiler::GRecorder = nullptr;
	bRecording = false;
	bEnabled = false;
	LOG( LogProfiler, Log, "Shutdown (%d frame(s) stored, %u string(s) interned, %d worker thread(s) seen)", GetNumFrames(), NumStrings.Load(),
		 WorkerBuffers.Num() );

	// このスレッドの控えがこのプロファイラーを指したままにならないように
	if ( GThreadBufferOwner == this )
	{
		GThreadBuffer = nullptr;
		GThreadBufferOwner = nullptr;
	}
	{
		FScopedLock Lock( WorkerRegistryMutex );
		WorkerBuffers.Reset();
	}
	for ( TAtomic<FString*>& Block : StringBlocks )
	{
		delete[] Block.Exchange( nullptr );
	}
	NumStrings.Store( 0 );
}

void CProfilerSubsystem::MeasureOverhead()
{
	constexpr int32 NumIterations = 4096;

	const int32 SavedMax = MaxSamplesPerFrame;
	MaxSamplesPerFrame = NumIterations * 2;
	Current.Samples.Reset();
	Current.Samples.Reserve( NumIterations * 2 );
	CurrentTop = INDEX_NONE;
	bRecording = true;

	const int64 Start = NPlatformTime::GetTicks();
	for ( int32 i = 0; i < NumIterations; ++i )
	{
		EndSample( BeginSample( 0, 0, 0, 0, 0 ) );
	}
	const int64 End = NPlatformTime::GetTicks();

	bRecording = false;
	CurrentTop = INDEX_NONE;
	Current.Samples.Reset();
	MaxSamplesPerFrame = SavedMax;

	OverheadNsPerSample = NPlatformTime::TicksToUs( End - Start ) * 1000.0 / static_cast<float64>( NumIterations );
}

#pragma endregion

#pragma region Control

void CProfilerSubsystem::SetEnabled( bool bInEnabled )
{
	if ( bEnabled == bInEnabled ) return;
	bEnabled = bInEnabled;
	if ( bEnabled )
	{
		bClearOnStart = true;
		bMemoryLimitReached = false;
	}
	if ( !bEnabled ) RemainingCaptureFrames = 0;
	LOG( LogProfiler, Log, "%s (takes effect at the next frame)", bEnabled ? "Start requested" : "Stop requested" );
}

void CProfilerSubsystem::CaptureFrames( int32 NumFrames )
{
	if ( NumFrames <= 0 ) return;
	RemainingCaptureFrames = NumFrames;
	if ( !bEnabled ) bClearOnStart = true;
	bEnabled = true;
	LOG( LogProfiler, Log, "Capturing %d frame(s)", NumFrames );
}

void CProfilerSubsystem::Clear()
{
	Frames.Empty();   // メモリも返す
	FirstFrameIndex = 0;
	StoredSamples = 0;
	StoredCounters = 0;
	bMemoryLimitReached = false;
	ClearWorkerBuffers();
	LOG( LogProfiler, Verbose, "Cleared" );
}

void CProfilerSubsystem::SetMaxFrames( int32 NumFrames )
{
	MaxFrames = NumFrames < 0 ? 0 : NumFrames;
	EnforceLimits();
	LOG( LogProfiler, Verbose, "History limit: %d frame(s) (0 = unlimited)", MaxFrames );
}

void CProfilerSubsystem::SetMaxSamplesPerFrame( int32 NumSamples )
{
	MaxSamplesPerFrame = NumSamples < MinMaxSamples ? MinMaxSamples : NumSamples;
}

void CProfilerSubsystem::SetMaxStoredSamples( int32 NumSamples )
{
	MaxStoredSamples = NumSamples <= 0 ? 0 : ( NumSamples < 10000 ? 10000 : NumSamples );
	EnforceLimits();
	LOG( LogProfiler, Verbose, "Stored sample limit: %d (0 = unlimited)", MaxStoredSamples );
}

void CProfilerSubsystem::SetMemoryLimitMB( int32 Megabytes )
{
	MemoryLimitMB = Megabytes < 0 ? 0 : Megabytes;
	LOG( LogProfiler, Verbose, "Memory limit: %d MB (0 = unlimited)", MemoryLimitMB );
}

int64 CProfilerSubsystem::GetStoredBytes() const
{
	return StoredSamples * static_cast<int64>( sizeof( FProfileSample ) ) + StoredCounters * static_cast<int64>( sizeof( FProfileCounter ) )
		   + static_cast<int64>( GetNumFrames() ) * static_cast<int64>( sizeof( FProfilerFrame ) );
}

void CProfilerSubsystem::DropOldestFrame()
{
	FProfilerFrame& Oldest = Frames[FirstFrameIndex];
	StoredSamples -= Oldest.Samples.Num();
	for ( const FProfilerThreadTrack& Track : Oldest.Threads ) StoredSamples -= Track.Samples.Num();
	StoredCounters -= Oldest.Counters.Num();
	Oldest.Samples.Empty();   // メモリも返す
	Oldest.Threads.Empty();
	Oldest.Counters.Empty();
	++FirstFrameIndex;

	// 先頭の空きが増えたら詰める（毎回詰めると遅いので、まとめて）
	if ( FirstFrameIndex >= 1024 && FirstFrameIndex * 2 >= Frames.Num() )
	{
		Frames.RemoveAt( 0, FirstFrameIndex );
		FirstFrameIndex = 0;
	}
}

void CProfilerSubsystem::EnforceLimits()
{
	// 新しいフレームは必ず 1 つ残す
	while ( MaxFrames > 0 && GetNumFrames() > MaxFrames && GetNumFrames() > 1 ) DropOldestFrame();
	while ( MaxStoredSamples > 0 && StoredSamples > MaxStoredSamples && GetNumFrames() > 1 ) DropOldestFrame();
}

#pragma endregion

#pragma region Frame

void CProfilerSubsystem::BeginFrame()
{
	const bool bWasRecording = bRecording;
	if ( bRecording ) FinishFrame();

	if ( bEnabled )
	{
		StartFrame();
	}
	else
	{
		bRecording = false;
		NProfiler::GRecorder = nullptr;

		// 記録が今止まった
		if ( bWasRecording )
		{
			LOG( LogProfiler, Log, "Recording stopped (%d frame(s) stored, ~%.1f MB)", GetNumFrames(), static_cast<float64>( GetStoredBytes() ) / BytesPerMegabyte );
			LogSummary();
		}
	}
}

void CProfilerSubsystem::LogSummary() const
{
	const int32 NumStored = GetNumFrames();
	if ( NumStored <= 0 ) return;

	// 長く記録したときに止まらないよう、要約は直近の SummaryMaxFrames フレームまで
	const int32 FirstFrame = NumStored > SummaryMaxFrames ? NumStored - SummaryMaxFrames : 0;
	const FString Scope = FirstFrame > 0 ? FString::Printf( " (latest; %d stored)", NumStored ) : FString();

	const FProfileFrameStats Stats = NProfilerAnalyzer::ComputeFrameStats( *this, FirstFrame, -1 );
	LOG( LogProfiler, Log, "Summary: %d frame(s)%s, average %.2f ms (%.0f FPS), min %.2f ms, max %.2f ms, %.0f samples/frame",
		 Stats.NumFrames, *Scope, Stats.AverageMs, Stats.AverageMs > 0.0 ? 1000.0 / Stats.AverageMs : 0.0, Stats.MinMs, Stats.MaxMs,
		 static_cast<float64>( Stats.NumSamples ) / Stats.NumFrames );

	// 子を除いた時間（Self）の大きい順に、Category + Name ごとの上位を出す
	FProfileQuery Query;
	Query.FirstFrame = FirstFrame;
	Query.GroupBy = NProfileGroupBy::Category | NProfileGroupBy::Name;
	TArray<FProfileStatRow> Rows;
	NProfilerAnalyzer::BuildStats( *this, Query, Rows );
	Rows.StableSort( []( const FProfileStatRow& A, const FProfileStatRow& B ) { return A.SelfMs > B.SelfMs; } );

	const float64 NumFrames = static_cast<float64>( Stats.NumFrames );
	const int32 NumTop = Rows.Num() < 8 ? Rows.Num() : 8;
	for ( int32 i = 0; i < NumTop; ++i )
	{
		const FProfileStatRow& Row = Rows[i];
		LOG( LogProfiler, Log, "  #%d %s: self %.3f ms/frame (%.1f%%), total %.3f ms/frame, %.1f calls/frame", i + 1, *Row.Label,
			 Row.SelfMs / NumFrames, Stats.AverageMs > 0.0 ? 100.0 * Row.SelfMs / NumFrames / Stats.AverageMs : 0.0, Row.InclusiveMs / NumFrames,
			 Row.Calls / NumFrames );
	}
}

void CProfilerSubsystem::StartFrame()
{
	if ( bClearOnStart )
	{
		bClearOnStart = false;
		Clear();
		LOG( LogProfiler, Log, "Recording started" );
	}

	Current.Samples.Reset();
	Current.Threads.Reset();
	{
		FScopedLock Lock( CounterMutex );
		Current.Counters.Reset();
	}
	Current.NumDropped = 0;
	Current.MaxDepth = 0;
	Current.FrameNumber = NextFrameNumber++;
	Current.StartTick = NPlatformTime::GetTicks();
	Current.EndTick = Current.StartTick;
	CurrentTop = INDEX_NONE;

	bRecording = true;
	NProfiler::GRecorder = this;
}

void CProfilerSubsystem::FinishFrame()
{
	Current.EndTick = NPlatformTime::GetTicks();
	bRecording = false;
	NProfiler::GRecorder = nullptr;
	CurrentTop = INDEX_NONE;

	// 別スレッドで完成した木を集める（このフレームの分は Current へ、前のフレームで始まった分は保存済みのそのフレームへ）
	CollectWorkerSamples();

	// 保存するときは、サンプル数ちょうどの大きさにコピーする（作業用の Current は容量を使い回し、保存した分に余分なメモリを持たせない）
	const int32 StoredIndex = Frames.Emplace();
	FProfilerFrame& Stored = Frames[StoredIndex];
	Stored.FrameNumber = Current.FrameNumber;
	Stored.StartTick = Current.StartTick;
	Stored.EndTick = Current.EndTick;
	Stored.NumDropped = Current.NumDropped;
	Stored.MaxDepth = Current.MaxDepth;
	Stored.Samples.Append( Current.Samples.GetData(), Current.Samples.Num() );
	Stored.Threads = MoveTemp( Current.Threads );
	{
		FScopedLock Lock( CounterMutex );
		Stored.Counters.Append( Current.Counters.GetData(), Current.Counters.Num() );
	}
	StoredCounters += Stored.Counters.Num();
	StoredSamples += Stored.Samples.Num();
	for ( const FProfilerThreadTrack& Track : Stored.Threads ) StoredSamples += Track.Samples.Num();

	// 上限を決めているときだけ、超えた分を古いフレームから捨てる
	EnforceLimits();

	// 履歴は捨てない代わりに、メモリの上限に達したら記録を止める
	if ( MemoryLimitMB > 0 && GetStoredBytes() >= static_cast<int64>( MemoryLimitMB ) * BytesPerMegabyte )
	{
		bEnabled = false;
		RemainingCaptureFrames = 0;
		bMemoryLimitReached = true;
		LOG( LogProfiler, Warning, "Memory limit reached (%d MB, %d frame(s)): recording stopped. The data is kept; raise the limit or Clear to record more.",
			 MemoryLimitMB, GetNumFrames() );
	}

	if ( RemainingCaptureFrames > 0 && --RemainingCaptureFrames == 0 )
	{
		bEnabled = false;
		LOG( LogProfiler, Log, "Capture finished (%d frame(s) stored)", GetNumFrames() );
	}
}

const FProfilerFrame& CProfilerSubsystem::GetFrame( int32 Index ) const
{
	return Frames[FirstFrameIndex + Index];
}

#pragma endregion

#pragma region Strings

uint32 CProfilerSubsystem::InternLocked( const FString& Key )
{
	if ( const uint32* Found = StringIds.Find( Key ) ) return *Found;

	const uint32 Id = NumStrings.Load( EMemoryOrder::Relaxed );
	const uint32 BlockIndex = Id / StringBlockSize;
	if ( BlockIndex >= static_cast<uint32>( MaxStringBlocks ) ) return 0;   // 上限（400 万文字列）。通常は届かない

	FString* Block = StringBlocks[BlockIndex].Load( EMemoryOrder::Relaxed );
	if ( Block == nullptr )
	{
		Block = new FString[StringBlockSize];
		StringBlocks[BlockIndex].Store( Block, EMemoryOrder::Release );
	}
	Block[Id % StringBlockSize] = Key;
	NumStrings.Store( Id + 1, EMemoryOrder::Release );   // 中身を書いてから公開する（読む側はロックしない）
	StringIds.Add( Key, Id );
	return Id;
}

uint32 CProfilerSubsystem::InternString( const char* Text )
{
	if ( Text == nullptr || Text[0] == '\0' ) return 0;

	const FString Key( SkipTypePrefix( Text ) );
	FScopedLock Lock( StringMutex );
	return InternLocked( Key );
}

uint32 CProfilerSubsystem::InternLiteral( const char* Literal )
{
	if ( Literal == nullptr || Literal[0] == '\0' ) return 0;

	// スレッドごとの小さな控え（ポインタ -> ID）。ほとんどの呼び出しはここで済み、ロックしない
	struct FCacheEntry
	{
		const char* Ptr;
		uint32 Id;
	};
	thread_local FCacheEntry Cache[256] = {};
	FCacheEntry& Entry = Cache[( ( reinterpret_cast<uintptr_t>( Literal ) >> 3 ) * 2654435761u >> 8 ) & 255];
	if ( Entry.Ptr == Literal ) return Entry.Id;

	uint32 Id;
	{
		FScopedLock Lock( StringMutex );
		if ( const uint32* Found = LiteralIds.Find( Literal ) )
		{
			Id = *Found;
		}
		else
		{
			Id = InternLocked( FString( SkipTypePrefix( Literal ) ) );
			LiteralIds.Add( Literal, Id );
		}
	}
	Entry.Ptr = Literal;
	Entry.Id = Id;
	return Id;
}

const FString& CProfilerSubsystem::GetString( uint32 Id ) const
{
	static const FString Empty;
	if ( Id == 0 || Id >= NumStrings.Load( EMemoryOrder::Acquire ) ) return Empty;

	const FString* Block = StringBlocks[Id / StringBlockSize].Load( EMemoryOrder::Acquire );
	return Block != nullptr ? Block[Id % StringBlockSize] : Empty;
}

#pragma endregion

#pragma region Samples

void CProfilerSubsystem::RecordCounter( uint32 Name, float64 Value, EProfileCounterUnit Unit )
{
	if ( !bRecording || Name == 0 ) return;

	FScopedLock Lock( CounterMutex );
	for ( FProfileCounter& Existing : Current.Counters )
	{
		if ( Existing.Name == Name )
		{
			Existing.Value = Value;
			Existing.Unit = Unit;
			return;
		}
	}
	FProfileCounter& Counter = Current.Counters.EmplaceGetRef();
	Counter.Name = Name;
	Counter.Unit = Unit;
	Counter.Value = Value;
}

int64 CProfilerSubsystem::BeginSample( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label )
{
	// メインスレッド以外は、スレッドごとのバッファへ
	if ( NPlatformTime::GetCurrentThreadId() != RecordingThreadId ) return BeginWorkerSample( Category, Owner, Name, Instance, Label );
	if ( !bRecording ) return INDEX_NONE;

	if ( Current.Samples.Num() >= MaxSamplesPerFrame )
	{
		++Current.NumDropped;
		return INDEX_NONE;
	}

	FProfileSample& Sample = Current.Samples.EmplaceGetRef();
	Sample.Category = Category;
	Sample.Owner = Owner;
	Sample.Name = Name;
	Sample.Instance = Instance;
	Sample.Label = Label;
	Sample.Parent = CurrentTop;
	Sample.Depth = CurrentTop == INDEX_NONE ? 0 : static_cast<uint16>( Current.Samples[CurrentTop].Depth + 1 );
	if ( Sample.Depth > Current.MaxDepth ) Current.MaxDepth = Sample.Depth;

	const int32 Index = Current.Samples.Num() - 1;
	CurrentTop = Index;

	// 自分の準備の時間を含めないよう、最後に開始時刻を取る
	Sample.StartTick = NPlatformTime::GetTicks();
	return Index;
}

void CProfilerSubsystem::EndSample( int64 SampleId )
{
	// 先に終了時刻を取る
	const int64 EndTick = NPlatformTime::GetTicks();
	if ( SampleId == INDEX_NONE ) return;
	if ( NPlatformTime::GetCurrentThreadId() != RecordingThreadId )
	{
		EndWorkerSample( SampleId );
		return;
	}

	const int32 SampleIndex = static_cast<int32>( SampleId );
	if ( !bRecording || !Current.Samples.IsValidIndex( SampleIndex ) ) return;

	FProfileSample& Sample = Current.Samples[SampleIndex];
	Sample.DurationTick = EndTick - Sample.StartTick;
	if ( Sample.Parent != INDEX_NONE ) Current.Samples[Sample.Parent].ChildTick += Sample.DurationTick;
	CurrentTop = Sample.Parent;
}

#pragma endregion

#pragma region Worker threads

FProfilerWorkerBuffer* CProfilerSubsystem::GetWorkerBuffer()
{
	if ( GThreadBufferOwner == this ) return GThreadBuffer;

	TUniquePtr<FProfilerWorkerBuffer> Buffer = MakeUnique<FProfilerWorkerBuffer>();
	Buffer->ThreadId = NPlatformTime::GetCurrentThreadId();
	FProfilerWorkerBuffer* Raw = Buffer.Get();
	{
		FScopedLock Lock( WorkerRegistryMutex );
		WorkerBuffers.Add( MoveTemp( Buffer ) );
	}
	GThreadBuffer = Raw;
	GThreadBufferOwner = this;
	return Raw;
}

int64 CProfilerSubsystem::BeginWorkerSample( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label )
{
	FProfilerWorkerBuffer& Buffer = *GetWorkerBuffer();
	FScopedLock Lock( Buffer.Mutex );

	// スレッド名は、付けられたときに 1 回だけ ID に変える（ロックの順は Buffer -> String で、逆はないので詰まらない）
	if ( NProfiler::GThreadName != Buffer.NamePtr )
	{
		Buffer.NamePtr = NProfiler::GThreadName;
		Buffer.NameId = InternString( NProfiler::GThreadName );
	}

	if ( Buffer.Samples.Num() >= MaxWorkerBufferSamples ) return INDEX_NONE;

	FProfileSample& Sample = Buffer.Samples.EmplaceGetRef();
	Sample.Category = Category;
	Sample.Owner = Owner;
	Sample.Name = Name;
	Sample.Instance = Instance;
	Sample.Label = Label;
	Sample.Parent = Buffer.CurrentTop;
	Sample.Depth = Buffer.CurrentTop == INDEX_NONE ? 0 : static_cast<uint16>( Buffer.Samples[Buffer.CurrentTop].Depth + 1 );
	Sample.DurationTick = -1;   // 開いている印（終わると 0 以上になる）

	const int32 Index = Buffer.Samples.Num() - 1;
	Buffer.CurrentTop = Index;
	Sample.StartTick = NPlatformTime::GetTicks();
	return Buffer.BaseId + Index;
}

void CProfilerSubsystem::EndWorkerSample( int64 SampleId )
{
	if ( GThreadBufferOwner != this ) return;   // このスレッドは始めていない
	FProfilerWorkerBuffer& Buffer = *GThreadBuffer;

	const int64 EndTick = NPlatformTime::GetTicks();
	FScopedLock Lock( Buffer.Mutex );

	const int64 Index = SampleId - Buffer.BaseId;
	if ( Index < 0 || Index >= Buffer.Samples.Num() ) return;   // 記録が消されたあとなど

	FProfileSample& Sample = Buffer.Samples[static_cast<int32>( Index )];
	Sample.DurationTick = EndTick - Sample.StartTick;
	if ( Sample.Parent != INDEX_NONE ) Buffer.Samples[Sample.Parent].ChildTick += Sample.DurationTick;
	Buffer.CurrentTop = Sample.Parent;
}

void CProfilerSubsystem::CollectWorkerSamples()
{
	TArray<FProfilerWorkerBuffer*> Buffers;
	{
		FScopedLock Lock( WorkerRegistryMutex );
		Buffers.Reserve( WorkerBuffers.Num() );
		for ( const TUniquePtr<FProfilerWorkerBuffer>& Buffer : WorkerBuffers ) Buffers.Add( Buffer.Get() );
	}

	TArray<FProfileSample> Taken;
	for ( FProfilerWorkerBuffer* Buffer : Buffers )
	{
		Taken.Reset();
		{
			FScopedLock Lock( Buffer->Mutex );

			// 先頭から、まだ開いている根（ジョブの途中）の手前までが、完成した木。サンプルは呼び出しの前順なので、ここまでは木が丸ごと揃っている
			int32 NumComplete = 0;
			const int32 Num = Buffer->Samples.Num();
			while ( NumComplete < Num )
			{
				const FProfileSample& Sample = Buffer->Samples[NumComplete];
				if ( Sample.Parent == INDEX_NONE && Sample.DurationTick < 0 ) break;
				++NumComplete;
			}
			if ( NumComplete == 0 ) continue;

			Taken.Append( Buffer->Samples.GetData(), NumComplete );
			Buffer->Samples.RemoveAt( 0, NumComplete );
			Buffer->BaseId += NumComplete;
			for ( FProfileSample& Sample : Buffer->Samples )
			{
				if ( Sample.Parent != INDEX_NONE ) Sample.Parent -= NumComplete;
			}
			if ( Buffer->CurrentTop != INDEX_NONE ) Buffer->CurrentTop -= NumComplete;
		}

		// 木（根から次の根の手前まで）ごとに、開始時刻のフレームへ振り分ける
		int32 Begin = 0;
		while ( Begin < Taken.Num() )
		{
			int32 End = Begin + 1;
			while ( End < Taken.Num() && Taken[End].Parent != INDEX_NONE ) ++End;
			AddWorkerTree( *Buffer, Taken.GetData() + Begin, End - Begin, Begin );
			Begin = End;
		}
	}
}

void CProfilerSubsystem::AddWorkerTree( const FProfilerWorkerBuffer& Buffer, const FProfileSample* Samples, int32 Count, int32 TreeStart )
{
	// 木が入るフレーム = 根が始まった時刻を含むフレーム（作業中の Current か、保存済みのフレーム）
	const int64 RootStart = Samples[0].StartTick;
	FProfilerFrame* Target = nullptr;
	bool bStoredFrame = false;
	if ( RootStart >= Current.StartTick )
	{
		Target = &Current;
	}
	else
	{
		// StartTick が RootStart 以下の最後のフレーム
		int32 Low = FirstFrameIndex, High = Frames.Num();
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Frames[Mid].StartTick <= RootStart ) Low = Mid + 1;
			else High = Mid;
		}
		if ( Low - 1 < FirstFrameIndex ) return;   // そのフレームはもう捨てた（または記録を始める前）
		Target = &Frames[Low - 1];
		bStoredFrame = true;
	}

	// このスレッドの行（なければ作る）
	FProfilerThreadTrack* Track = nullptr;
	for ( FProfilerThreadTrack& Existing : Target->Threads )
	{
		if ( Existing.ThreadId == Buffer.ThreadId )
		{
			Track = &Existing;
			break;
		}
	}
	if ( Track == nullptr )
	{
		Track = &Target->Threads.EmplaceGetRef();
		Track->ThreadId = Buffer.ThreadId;
	}
	if ( Buffer.NameId != 0 ) Track->Name = Buffer.NameId;

	// 追記する。Parent は木の先頭を 0 とした位置から、行の中での位置へ付け替える
	const int32 Offset = Track->Samples.Num();
	Track->Samples.Append( Samples, Count );
	for ( int32 i = 0; i < Count; ++i )
	{
		FProfileSample& Sample = Track->Samples[Offset + i];
		if ( Sample.Parent != INDEX_NONE ) Sample.Parent = Sample.Parent - TreeStart + Offset;
		if ( Sample.Depth > Track->MaxDepth ) Track->MaxDepth = Sample.Depth;
	}
	if ( bStoredFrame ) StoredSamples += Count;
}

void CProfilerSubsystem::ClearWorkerBuffers()
{
	FScopedLock RegistryLock( WorkerRegistryMutex );
	for ( const TUniquePtr<FProfilerWorkerBuffer>& Buffer : WorkerBuffers )
	{
		FScopedLock Lock( Buffer->Mutex );
		Buffer->BaseId += Buffer->Samples.Num();   // 開いたままのサンプルの EndSample は、番号が範囲外になって無視される
		Buffer->Samples.Reset();
		Buffer->CurrentTop = INDEX_NONE;
	}
}

#pragma endregion
