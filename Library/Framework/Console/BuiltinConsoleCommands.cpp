// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: コンソールそのもの（help・clear・history・echo・exec・alias など）
void NConsole::NBuiltin::RegisterConsoleCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "help", "Show the list of commands, or the usage of one command",
		 []( FConsoleContext& Context, TOptional<FString> Name ) { return PrintHelp( Context, Name ); },
		 { { "command", "Name of the command (Tab completes it). Without it, all commands are listed." } }, CategoryConsole );
	Registry.SetArgCompleter( "help", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() ) Names.Add( Info->Name );
		return Names;
	} );

	Add( "find", "Search the command names and descriptions for a text",
		 []( FConsoleContext& Context, const FString& Text )
		 {
			 int32 Count = 0;
			 for ( const FConsoleCommandInfo* Info : Context.GetRegistry().GetCommands() )
			 {
				 if ( Info->Name.Contains( Text, ESearchCase::IgnoreCase ) || Info->Description.Contains( Text, ESearchCase::IgnoreCase ) )
				 {
					 Context.Print( "  " + Pad( Info->Name, 22 ) + Info->Description );
					 ++Count;
				 }
			 }
			 if ( Count == 0 ) Context.Print( "no command matches '" + Text + "'" );
		 },
		 { { "text", "Part of a command name or description (not case sensitive)." } }, CategoryConsole );

	Add( "echo", "Print a text to the console",
		 []( const FConsoleRest& Text ) { return Text.Text; }, { { "text", "The text to print (all the rest of the line)." } }, CategoryConsole );

	Add( "clear", "Clear the console output", []( FConsoleContext& Context ) { Context.ClearOutput(); }, {}, CategoryConsole );

	Add( "history", "Show the commands entered before",
		 []( FConsoleContext& Context, TOptional<int32> Count )
		 {
			 const FConsoleHistory& History = GetGlobalHistory();
			 const int32 Show = Count.IsSet() ? NMath::Max( Count.GetValue(), 1 ) : 20;
			 const int32 First = NMath::Max( History.Num() - Show, 0 );
			 for ( int32 i = First; i < History.Num(); ++i ) Context.Print( FString::Printf( "  %3d  %s", i + 1, *History.Get( i ) ) );
			 if ( History.Num() == 0 ) Context.Print( "(no history yet)" );
		 },
		 { { "count", "How many of the latest entries to show (default 20)." } }, CategoryConsole );

	Add( "alias", "Define, show or list aliases (a short name for a command line)",
		 []( FConsoleContext& Context, TOptional<FString> Name, TOptional<FConsoleRest> )
		 {
			 FConsoleRegistry& Registry = Context.GetRegistry();
			 if ( !Name.IsSet() )
			 {
				 const TArray<TPair<FString, FString>> Aliases = Registry.GetAliases();
				 if ( Aliases.IsEmpty() ) Context.Print( "(no aliases) Define one with: alias <name> <command line>" );
				 for ( const TPair<FString, FString>& Alias : Aliases ) Context.Print( "  " + Pad( Alias.Key, 16 ) + Alias.Value );
				 return true;
			 }
			 if ( Context.GetArgs().Num() < 2 )
			 {
				 const FString* Line = Registry.FindAlias( Name.GetValue() );
				 if ( Line == nullptr )
				 {
					 Context.Error( "no alias named '" + Name.GetValue() + "'" );
					 return false;
				 }
				 Context.Print( Name.GetValue() + " = " + *Line );
				 return true;
			 }
			 FString Error;
			 if ( !Registry.SetAlias( Name.GetValue(), JoinQuoted( Context.GetArgs(), 1 ), Error ) )
			 {
				 Context.Error( Error );
				 return false;
			 }
			 Context.Print( "alias " + Name.GetValue() + " = " + JoinQuoted( Context.GetArgs(), 1 ) );
			 return true;
		 },
		 { { "name", "The alias name. Without a command line, shows that alias." }, { "command", "The command line the alias runs. Arguments typed after the alias are added to its end." } }, CategoryConsole );

	Add( "unalias", "Remove an alias",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 if ( Context.GetRegistry().RemoveAlias( Name ) ) return true;
			 Context.Error( "no alias named '" + Name + "'" );
			 return false;
		 },
		 { "name" }, CategoryConsole );

	Add( "exec", "Run the commands in a text file (one per line; lines starting with # or // are comments)",
		 []( FConsoleContext& Context, const FString& Path )
		 {
			 FString Error;
			 int32 NumCommands = 0, NumFailed = 0;
			 if ( !ExecuteScriptFile( Context.GetRegistry(), Path, Context.GetOutput(), Error, &NumCommands, &NumFailed ) )
			 {
				 Context.Error( Error );
				 return false;
			 }
			 Context.Print( FString::Printf( "ran %d command(s) from %s%s", NumCommands, *Path, NumFailed > 0 ? *FString::Printf( ", %d failed", NumFailed ) : "" ) );
			 return NumFailed == 0;
		 },
		 { { "file", "Path of the script. Searched in the working folder, near the executable, then in the project's Config folder." } }, CategoryConsole );

	Add( "repeat", "Run a command line several times",
		 []( FConsoleContext& Context, int32 Count, const FConsoleRest& )
		 {
			 if ( Count < 1 || Count > 1000 )
			 {
				 Context.Error( "count must be between 1 and 1000" );
				 return false;
			 }
			 const FString Line = JoinQuoted( Context.GetArgs(), 1 );
			 if ( Line.IsEmpty() )
			 {
				 Context.Error( "give a command line to repeat: repeat <count> <command...>" );
				 return false;
			 }
			 bool bOk = true;
			 for ( int32 i = 0; i < Count; ++i )
			 {
				 const EConsoleResult Result = Context.GetRegistry().Execute( Line, Context.GetOutput() );
				 bOk = bOk && ( Result == EConsoleResult::Ok );
			 }
			 return bOk;
		 },
		 { { "count", "How many times (1 - 1000)." }, { "command", "The command line to run." } }, CategoryConsole );

	Add( "quit", "Exit the application",
		 []( FConsoleContext& Context )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 Context.Print( "exiting..." );
			 Platform->RequestExit();
			 return true;
		 },
		 {}, CategoryConsole );
	Add( "exit", "Exit the application (same as quit)", []( FConsoleContext& Context ) { return Context.GetRegistry().Execute( "quit", Context.GetOutput() ) == EConsoleResult::Ok; }, {}, CategoryConsole );
}
