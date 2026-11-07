// SPDX-License-Identifier: MIT
#include "EngineCrashHandlers.h"

#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Profiler/ProfilerExport.h"
#include "Library/Platform/System/CrashReporter.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogEngineCrash, Log )

namespace
{
	// 直近のフレームだけを書く（長く走っていても、書き出しが長引かないように）
	constexpr int32 MaxTraceFrames = 600;

	void StopAllSounds( const FCrashInfo& )
	{
		if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
		{
			Audio->StopAll();
		}
	}

	void SaveProfilerTrace( const FCrashInfo& )
	{
		const IProfiler* Profiler = GetAppSubsystem<IProfiler>();
		if ( Profiler == nullptr || Profiler->GetNumFrames() == 0 )
		{
			return;
		}
		FString Path = NProfilerExport::MakeDefaultTracePath();
		if ( Path.EndsWith( ".json" ) )
		{
			Path = Path.Left( Path.Len() - 5 ) + "_crash.json";
		}
		const int32 Slash = Path.Replace( "\\", "/" ).FindLast( "/" );
		if ( Slash > 0 )
		{
			NFileSystem::EnsureDirectory( Path.Replace( "\\", "/" ).Left( Slash ) );
		}
		const int32 Last = Profiler->GetNumFrames() - 1;
		const int32 First = Last - MaxTraceFrames + 1 > 0 ? Last - MaxTraceFrames + 1 : 0;
		if ( NProfilerExport::WriteChromeTrace( *Profiler, First, Last, Path ) )
		{
			LOG( LogEngineCrash, Error, "Profiler trace saved: %s (%d frame(s))", *Path, Last - First + 1 );
		}
	}
}

void NEngineCrashHandlers::Register()
{
	NCrashReporter::AddEngineCrashHandler( StopAllSoundsName, &StopAllSounds );
	NCrashReporter::AddEngineCrashHandler( SaveProfilerTraceName, &SaveProfilerTrace );
}
