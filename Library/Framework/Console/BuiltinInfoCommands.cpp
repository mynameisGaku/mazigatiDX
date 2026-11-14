// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: 情報（バージョン・メモリなど）
void NConsole::NBuiltin::RegisterInfoCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "version", "Show the build configuration, the compiler and the build date",
		 []()
		 {
#if defined( _DEBUG )
			 const char* Configuration = "Debug";
#else
			 const char* Configuration = "Release";
#endif
			 return FString::Printf( "%s build, MSVC %d, built %s %s, debug console %s", Configuration, static_cast<int>( _MSC_VER ), __DATE__, __TIME__, COG_DEBUG_CONSOLE ? "enabled" : "disabled" );
		 },
		 {}, CategoryInfo );

	Add( "cmdline", "Show the command line the application was started with",
		 []() { return NCommandLine::Get(); }, {}, CategoryInfo );

	Add( "cwd", "Show the working folder, the executable folder and the project root",
		 []( FConsoleContext& Context )
		 {
			 Context.Print( "working folder:    " + NFileSystem::GetWorkingDirectory() );
			 Context.Print( "executable folder: " + NFileSystem::GetExecutableDirectory() );
			 Context.Print( "project root:      " + NFileSystem::FindProjectRoot() );
		 },
		 {}, CategoryInfo );

	Add( "date", "Show the current local date and time",
		 []()
		 {
			 const FFileTime Now = NFileSystem::GetCurrentLocalTime();
			 return FString::Printf( "%04d-%02d-%02d %02d:%02d:%02d", Now.Year, Now.Month, Now.Day, Now.Hour, Now.Minute, Now.Second );
		 },
		 {}, CategoryInfo );
}
