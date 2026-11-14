// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: プロファイラー
void NConsole::NBuiltin::RegisterProfilerCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "profiler", "Turn the profiler on or off, or show whether it is recording",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( On.IsSet() ) Profiler->SetEnabled( On.GetValue() );
			 Context.Print( Profiler->IsEnabled() ? "profiler: recording" : "profiler: off" );
			 return true;
		 },
		 { { "on", "true to start recording, false to stop. Without it, shows the state." } }, CategoryProfiler );
}
