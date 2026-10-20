#include "ProfilerExport.h"

#include <cstdio>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogProfilerExport, Log )

namespace
{
	// JSON の文字列として書き出せる形（UTF-8、" \ と制御文字をエスケープ）にする
	FString EscapeJson( const FString& Text )
	{
		const FString Utf8 = Text.ToUtf8();
		FString Result;
		Result.Reserve( Utf8.Len() + 8 );
		for ( int32 i = 0; i < Utf8.Len(); ++i )
		{
			const char Ch = Utf8[i];
			switch ( Ch )
			{
			case '"':  Result += "\\\""; break;
			case '\\': Result += "\\\\"; break;
			case '\n': Result += "\\n"; break;
			case '\r': Result += "\\r"; break;
			case '\t': Result += "\\t"; break;
			default:
				if ( static_cast<unsigned char>( Ch ) < 0x20 ) Result += FString::Printf( "\\u%04x", static_cast<unsigned>( Ch ) );
				else Result += Ch;
				break;
			}
		}
		return Result;
	}

	// 開始時刻を揃えた相対マイクロ秒
	FORCEINLINE float64 ToMicroseconds( int64 Tick, int64 BaseTick )
	{
		return NPlatformTime::TicksToUs( Tick - BaseTick );
	}

	constexpr int32 MainThreadId = 1;
	constexpr int32 FrameTrackId = 2;
	constexpr int32 FirstWorkerTrackId = 100;   // メインスレッド以外（ジョブシステムのワーカーなど）は、見つかった順にここから
}

FString NProfilerExport::MakeDefaultTracePath()
{
	const FString Name = NFileSystem::GetExecutableName() + "_" + NFileSystem::GetCurrentLocalTime().ToFileNameString() + ".json";
	return NFileSystem::FindProjectRoot() / "Saved" / "Profiler" / Name;
}

bool NProfilerExport::WriteChromeTrace( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, const FString& Path )
{
	if ( !NProfilerAnalyzer::ClampFrameRange( Profiler, FirstFrame, LastFrame ) )
	{
		LOG( LogProfilerExport, Warning, "Nothing to export: no frames have been recorded." );
		return false;
	}

	// 保存先のフォルダを作る
	const int32 SeparatorIndex = Path.Replace( "/", "\\" ).FindLast( "\\" );
	if ( SeparatorIndex > 0 && !NFileSystem::EnsureDirectory( Path.Left( SeparatorIndex ) ) )
	{
		LOG( LogProfilerExport, Error, "Failed to create the folder for %s", *Path );
		return false;
	}

	FILE* File = nullptr;
	if ( fopen_s( &File, *Path, "wb" ) != 0 || File == nullptr )
	{
		LOG( LogProfilerExport, Error, "Failed to open %s for writing", *Path );
		return false;
	}

	const int64 BaseTick = Profiler.GetFrame( FirstFrame ).StartTick;
	int64 NumEvents = 0;

	std::fputs( "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n", File );

	// プロセス名・トラック名
	const FString ProcessName = EscapeJson( NFileSystem::GetExecutableName() );
	std::fprintf( File, "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":\"%s\"}}", *ProcessName );
	std::fprintf( File, ",\n{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%d,\"args\":{\"name\":\"Main thread\"}}", MainThreadId );
	std::fprintf( File, ",\n{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%d,\"args\":{\"name\":\"Frames\"}}", FrameTrackId );

	// メインスレッド以外（ジョブシステムのワーカーなど）。範囲の中で見つかった順に、トラックを割り当てる
	TArray<uint32> WorkerThreadIds;
	TArray<uint32> WorkerThreadNames;
	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		for ( const FProfilerThreadTrack& Track : Profiler.GetFrame( FrameIndex ).Threads )
		{
			int32 Found = WorkerThreadIds.Find( Track.ThreadId );
			if ( Found == INDEX_NONE )
			{
				Found = WorkerThreadIds.Add( Track.ThreadId );
				WorkerThreadNames.Add( 0 );
			}
			if ( Track.Name != 0 ) WorkerThreadNames[Found] = Track.Name;
		}
	}
	for ( int32 i = 0; i < WorkerThreadIds.Num(); ++i )
	{
		const FString Name = WorkerThreadNames[i] != 0 ? Profiler.GetString( WorkerThreadNames[i] ) : FString::Printf( "Thread %u", static_cast<unsigned>( WorkerThreadIds[i] ) );
		std::fprintf( File, ",\n{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%d,\"args\":{\"name\":\"%s\"}}", FirstWorkerTrackId + i, *EscapeJson( Name ) );
	}

	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		const FProfilerFrame& Frame = Profiler.GetFrame( FrameIndex );

		// フレーム全体（専用トラック）
		std::fprintf( File, ",\n{\"name\":\"Frame %llu\",\"cat\":\"Frame\",\"ph\":\"X\",\"pid\":1,\"tid\":%d,\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"samples\":%d,\"dropped\":%d}}",
					  static_cast<unsigned long long>( Frame.FrameNumber ), FrameTrackId, ToMicroseconds( Frame.StartTick, BaseTick ),
					  NPlatformTime::TicksToUs( Frame.GetDurationTick() ), Frame.Samples.Num(), Frame.NumDropped );
		++NumEvents;

		// カウンター（Chrome トレースの "C" イベント。フレームの始まりの時刻に、値をグラフの 1 点として置く）
		for ( const FProfileCounter& Counter : Frame.Counters )
		{
			std::fprintf( File, ",\n{\"name\":\"%s\",\"cat\":\"Counter\",\"ph\":\"C\",\"pid\":1,\"ts\":%.3f,\"args\":{\"value\":%.6g}}",
						  *EscapeJson( Profiler.GetString( Counter.Name ) ), ToMicroseconds( Frame.StartTick, BaseTick ), Counter.Value );
			++NumEvents;
		}

		// メインスレッド（TrackIndex = -1）と、メインスレッド以外の行
		for ( int32 TrackIndex = -1; TrackIndex < Frame.Threads.Num(); ++TrackIndex )
		{
			const TArray<FProfileSample>& TrackSamples = TrackIndex < 0 ? Frame.Samples : Frame.Threads[TrackIndex].Samples;
			const int32 ThreadTrackId = TrackIndex < 0 ? MainThreadId : FirstWorkerTrackId + WorkerThreadIds.Find( Frame.Threads[TrackIndex].ThreadId );

			for ( const FProfileSample& Sample : TrackSamples )
			{
				const FString Name = EscapeJson( NProfilerAnalyzer::MakeInstanceLabel( Profiler, Sample.Instance, Sample.Label ).IsEmpty()
													 ? Profiler.GetString( Sample.Name )
													 : Profiler.GetString( Sample.Name ) + " " + NProfilerAnalyzer::MakeInstanceLabel( Profiler, Sample.Instance, Sample.Label ) );
				const FString Category = EscapeJson( Profiler.GetString( Sample.Category ) );
				const FString Owner = EscapeJson( Profiler.GetString( Sample.Owner ) );

				std::fprintf( File, ",\n{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":%d,\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"owner\":\"%s\",\"instance\":%llu,\"self_us\":%.3f,\"depth\":%u}}",
							  *Name, *Category, ThreadTrackId, ToMicroseconds( Sample.StartTick, BaseTick ), NPlatformTime::TicksToUs( Sample.DurationTick ),
							  *Owner, static_cast<unsigned long long>( Sample.Instance ), NPlatformTime::TicksToUs( Sample.GetSelfTick() ),
							  static_cast<unsigned>( Sample.Depth ) );
				++NumEvents;
			}
		}
	}

	std::fputs( "\n]}\n", File );
	const bool bOk = std::ferror( File ) == 0;
	std::fclose( File );

	if ( bOk ) LOG( LogProfilerExport, Log, "Exported %d frame(s), %lld event(s) to %s", LastFrame - FirstFrame + 1, static_cast<long long>( NumEvents ), *Path );
	else LOG( LogProfilerExport, Error, "Failed while writing %s (disk full?)", *Path );
	return bOk;
}
