#include "AssetSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Console/BuiltinCommands.h"
#include "Library/Framework/Console/ConsoleParse.h"
#include "Library/Framework/Console/ConsoleRegistry.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Engine/TimeControl.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Platform/FileSystem.h"

using namespace NAssetTest;

//=============================================================================
// デバッグ用のコンソール（コマンドの解析・登録・実行・補完・履歴・組み込みのコマンド）の確認
//=============================================================================
namespace
{
	struct FRun
	{
		EConsoleResult Result = EConsoleResult::Ok;
		TArray<FConsoleLine> Lines;
		FString All;

		bool Has( const char* Text ) const { return All.Contains( Text ); }
		bool Has( const FString& Text ) const { return All.Contains( Text ); }
		bool HasError() const
		{
			for ( const FConsoleLine& Line : Lines )
			{
				if ( Line.Kind == EConsoleLine::Error ) return true;
			}
			return false;
		}
		int32 CountLines( const char* Text ) const
		{
			int32 Count = 0;
			for ( const FConsoleLine& Line : Lines )
			{
				if ( Line.Text.Contains( Text ) ) ++Count;
			}
			return Count;
		}
	};

	FRun Run( FConsoleRegistry& Registry, const FString& Line )
	{
		FConsoleBuffer Out( 1000 );
		FRun Result;
		Result.Result = Registry.Execute( Line, Out );
		for ( int32 i = 0; i < Out.Num(); ++i ) Result.Lines.Add( Out.Get( i ) );
		Result.All = Out.ToText();
		return Result;
	}

	FString Tokens( const char* Text, bool* bOutOk = nullptr, FString* OutError = nullptr, int32* OutCount = nullptr )
	{
		TArray<FString> Result;
		FString Error;
		const bool bOk = NConsole::Tokenize( Text, Result, Error );
		if ( bOutOk != nullptr ) *bOutOk = bOk;
		if ( OutError != nullptr ) *OutError = Error;
		if ( OutCount != nullptr ) *OutCount = Result.Num();
		return FString::Join( Result, "|" );
	}

	//=========================================================================
	// 構文解析
	//=========================================================================
	void TestParse( FChecker& C )
	{
		C.Group = "console-parse";
		C.Check( Tokens( "a b  c" ) == "a|b|c" && Tokens( "  a\tb " ) == "a|b" && Tokens( "" ) == "", "arguments are split by spaces and tabs" );
		C.Check( Tokens( "say \"hello world\"" ) == "say|hello world", "double quotes hold spaces" );
		C.Check( Tokens( "'a b' c" ) == "a b|c" && Tokens( "'a\\b'" ) == "a\\b", "single quotes are taken as they are" );
		C.Check( Tokens( "a\"b c\"d" ) == "ab cd", "quoted and unquoted parts join into one argument" );
		C.Check( Tokens( "\"a\\\"b\"" ) == "a\"b" && Tokens( "\"a\\\\b\"" ) == "a\\b", "\\\" and \\\\ work in double quotes" );
		C.Check( Tokens( "\"x\\ny\\tz\"" ) == "x\ny\tz", "\\n and \\t work in double quotes" );
		C.Check( Tokens( "\"C:\\dir\\file\"" ) == "C:\\dir\\file", "other backslashes in double quotes stay (a Windows path can be written plainly)" );
		C.Check( Tokens( "C:\\dir\\file.txt" ) == "C:\\dir\\file.txt", "backslashes outside quotes are ordinary characters" );
		int32 Count = 0;
		Tokens( "\"\"", nullptr, nullptr, &Count );
		C.Check( Count == 1, "empty quotes make one empty argument" );
		Tokens( "a \"\" b", nullptr, nullptr, &Count );
		C.Check( Count == 3, "an empty argument in the middle is kept" );

		bool bOk = true;
		FString Error;
		Tokens( "say \"abc", &bOk, &Error );
		C.Check( !bOk && Error.Contains( "quote" ), "an unclosed double quote is an error" );
		Tokens( "say 'abc", &bOk, &Error );
		C.Check( !bOk, "an unclosed single quote is an error" );
		Tokens( "a;b", &bOk, &Error );
		C.Check( !bOk && Error.Contains( ";" ), "a bare ';' is refused by Tokenize (split the commands first)" );

		// Shift-JIS: 2 バイト文字の 2 バイト目が 0x5C（\）や 0x40.. でも、エスケープ・区切りとして扱わない
		const FString Hyo = "\x95\\";   // 表
		C.Check( Tokens( "\"\x95\\\"" ) == Hyo, "a Shift-JIS character ending in 0x5C does not escape the closing quote" );
		C.Check( Tokens( "\x95\\ x" ) == Hyo + "|x", "and outside quotes it stays whole" );
		C.Check( NConsole::SplitCommands( "echo \"\x95\\\"; fps" ).Num() == 2, "and ';' splitting still works after it" );

		// ';' で分ける
		const auto Split = []( const char* Line ) { return FString::Join( NConsole::SplitCommands( Line ), "|" ); };
		C.Check( Split( "a; b ;c" ) == "a|b|c", "commands are split at ';'" );
		C.Check( Split( "echo \"a;b\"; x" ) == "echo \"a;b\"|x", "a ';' inside quotes does not split" );
		C.Check( Split( ";;a;" ) == "a" && Split( "   " ) == "" && Split( "" ) == "", "empty pieces are dropped" );
		C.Check( Split( "# note" ) == "" && Split( "  // note" ) == "" && Split( "echo # not a comment" ) == "echo # not a comment", "comments are whole lines starting with # or //" );

		// Quote は、読み戻して同じになる
		bool bRoundTrip = true;
		for ( const char* Text : { "abc", "a b", "", "a\"b", "C:\\a b\\c", "x;y", "tab\there", "\x95\\ x", "it's", "line\nbreak" } )
		{
			TArray<FString> Parsed;
			FString ParseError;
			if ( !NConsole::Tokenize( NConsole::Quote( Text ), Parsed, ParseError ) || Parsed.Num() != 1 || Parsed[0] != Text ) bRoundTrip = false;
		}
		C.Check( bRoundTrip, "Quote() reads back to the same text" );
		C.Check( NConsole::Quote( "abc" ) == "abc" && NConsole::Quote( "a b" ) == "\"a b\"" && NConsole::Quote( "" ) == "\"\"", "Quote() only quotes what needs it" );

		// 入力中の場所
		NConsole::FInputAnalysis A = NConsole::AnalyzeInput( "echo hel" );
		C.Check( A.Tokens.Num() == 1 && A.Tokens[0] == "echo" && A.CurrentPrefix == "hel" && A.CurrentStart == 5 && !A.bInQuote, "typing the second word" );
		A = NConsole::AnalyzeInput( "echo " );
		C.Check( A.Tokens.Num() == 1 && A.CurrentPrefix.IsEmpty() && A.CurrentStart == 5, "after a space, a new word starts" );
		A = NConsole::AnalyzeInput( "echo \"my sc" );
		C.Check( A.Tokens.Num() == 1 && A.CurrentPrefix == "my sc" && A.CurrentStart == 5 && A.bInQuote, "inside an open quote" );
		A = NConsole::AnalyzeInput( "a; b c" );
		C.Check( A.Tokens.Num() == 1 && A.Tokens[0] == "b" && A.CurrentPrefix == "c", "only the last command counts" );
		A = NConsole::AnalyzeInput( "a;" );
		C.Check( A.Tokens.IsEmpty() && A.CurrentPrefix.IsEmpty() && A.CurrentStart == 2, "right after ';' a new command starts" );
		A = NConsole::AnalyzeInput( "" );
		C.Check( A.Tokens.IsEmpty() && A.CurrentStart == 0, "an empty line" );
	}

	//=========================================================================
	// 引数の型
	//=========================================================================
	void TestArgs( FChecker& C )
	{
		C.Group = "console-args";
		int64 Value = 0;
		C.Check( NConsole::ParseInt64( "42", Value ) && Value == 42 && NConsole::ParseInt64( "-7", Value ) && Value == -7 && NConsole::ParseInt64( "+5", Value ) && Value == 5, "integers" );
		C.Check( NConsole::ParseInt64( "0x10", Value ) && Value == 16 && NConsole::ParseInt64( "-0xff", Value ) && Value == -255 && NConsole::ParseInt64( "007", Value ) && Value == 7, "hexadecimal and leading zeros" );
		bool bReject = true;
		for ( const char* Text : { "", "12abc", "0x", "1.5", " 5", "5 ", "abc", "99999999999999999999", "--5" } ) bReject = bReject && !NConsole::ParseInt64( Text, Value );
		C.Check( bReject, "things that are not integers are refused" );

		float64 Number = 0.0;
		C.Check( NConsole::ParseFloat64( "1.5", Number ) && Number == 1.5 && NConsole::ParseFloat64( "-2", Number ) && Number == -2.0 && NConsole::ParseFloat64( ".5", Number ) && Number == 0.5, "numbers" );
		C.Check( NConsole::ParseFloat64( "1e3", Number ) && Number == 1000.0 && NConsole::ParseFloat64( "5.", Number ) && Number == 5.0, "exponents and a trailing dot" );
		bReject = true;
		for ( const char* Text : { "", "abc", "nan", "inf", "-inf", "1e999", "1,5", " 1", "1 ", "1.5x" } ) bReject = bReject && !NConsole::ParseFloat64( Text, Number );
		C.Check( bReject, "things that are not finite numbers are refused" );

		bool bBool = false;
		bool bAllBool = true;
		for ( const char* Text : { "true", "TRUE", "on", "On", "yes", "1" } ) bAllBool = bAllBool && NConsole::ParseBool( Text, bBool ) && bBool;
		for ( const char* Text : { "false", "False", "off", "OFF", "no", "0" } ) bAllBool = bAllBool && NConsole::ParseBool( Text, bBool ) && !bBool;
		C.Check( bAllBool && !NConsole::ParseBool( "maybe", bBool ) && !NConsole::ParseBool( "", bBool ) && !NConsole::ParseBool( "2", bBool ), "true / false in several spellings" );

		C.Check( NConsole::FormatFloat( 1.5 ) == "1.5" && NConsole::FormatFloat( 100.0 ) == "100" && NConsole::FormatFloat( 0.1 ) == "0.1" && NConsole::FormatFloat( -2.25 ) == "-2.25", "floats are shown without trailing zeros" );

		FString Error;
		int32 Int32 = 0;
		const FString Big = "3000000000";
		C.Check( !TConsoleArg<int32>::Parse( &Big, 1, Int32, Error ) && Error.Contains( "out of range" ), "an int32 argument rejects a value that does not fit" );
		uint32 Unsigned = 0;
		const FString Negative = "-1";
		C.Check( !TConsoleArg<uint32>::Parse( &Negative, 1, Unsigned, Error ) && Error.Contains( "range" ), "a uint32 argument rejects a negative value" );
		const FString VectorTokens[3] = { "1", "2.5", "-3" };
		FVector3 Vector;
		C.Check( TConsoleArg<FVector3>::Parse( VectorTokens, 3, Vector, Error ) && Vector.x == 1.0f && Vector.y == 2.5f && Vector.z == -3.0f, "a vec3 argument takes three numbers" );
		const FString BadVector[3] = { "1", "two", "3" };
		C.Check( !TConsoleArg<FVector3>::Parse( BadVector, 3, Vector, Error ) && Error.Contains( "two" ), "and names the bad one" );
		const FString RestTokens[3] = { "a", "b", "c" };
		FConsoleRest Rest;
		C.Check( TConsoleArg<FConsoleRest>::Parse( RestTokens, 3, Rest, Error ) && Rest.Text == "a b c", "a rest argument joins the remaining words" );
		C.Check( TConsoleArg<TOptional<int32>>::bOptional && TConsoleArg<TOptional<int32>>::NumTokens == 1 && TConsoleArg<FVector3>::NumTokens == 3 && TConsoleArg<FConsoleRest>::NumTokens == -1, "argument sizes" );
		C.Check( FString( TConsoleArg<float32>::TypeName() ) == "float" && FString( TConsoleArg<bool>::TypeName() ) == "bool" && TConsoleArg<FVector3>::ToText( FVector3( 1, 2, 3 ) ) == "1 2 3", "type names and value text" );
	}

	//=========================================================================
	// 出力の溜め場所・履歴・時間の進め方
	//=========================================================================
	void TestBufferHistoryTime( FChecker& C )
	{
		C.Group = "console-buffer";
		FConsoleBuffer Buffer( 3 );
		const uint64 Revision0 = Buffer.GetRevision();
		Buffer.Write( EConsoleLine::Info, "one\ntwo\r\nthree" );
		C.Check( Buffer.Num() == 3 && Buffer.Get( 0 ).Text == "one" && Buffer.Get( 1 ).Text == "two" && Buffer.Get( 2 ).Text == "three", "text is split into lines (CRLF too)" );
		C.Check( Buffer.GetRevision() > Revision0, "the revision grows when lines are added" );
		Buffer.Write( EConsoleLine::Error, "four" );
		C.Check( Buffer.Num() == 3 && Buffer.Get( 0 ).Text == "two" && Buffer.Get( 2 ).Text == "four" && Buffer.Get( 2 ).Kind == EConsoleLine::Error, "the oldest line is dropped at the limit" );
		Buffer.Write( EConsoleLine::Info, "" );
		C.Check( Buffer.Num() == 3 && Buffer.Get( 2 ).Text.IsEmpty(), "an empty text is one empty line" );
		C.Check( Buffer.ToText() == "three\nfour\n", "ToText joins the lines with newlines" );
		IConsoleOutput& Output = Buffer;
		Output.Clear();
		C.Check( Buffer.Num() == 0, "Clear through the output interface empties the buffer" );

		C.Group = "console-history";
		FConsoleHistory History( 3 );
		History.Add( "a" );
		History.Add( "" );
		History.Add( "   " );
		History.Add( "b" );
		History.Add( "  c  " );
		C.Check( History.Num() == 3 && History.Get( 0 ) == "a" && History.Get( 2 ) == "c", "empty lines are ignored and lines are trimmed" );
		History.Add( "a" );
		C.Check( History.Num() == 3 && History.Get( 0 ) == "b" && History.Get( 1 ) == "c" && History.Get( 2 ) == "a", "an old duplicate moves to the end" );
		History.Add( "d" );
		C.Check( History.Num() == 3 && History.Get( 0 ) == "c" && History.Get( 2 ) == "d", "the oldest is dropped at the limit" );

		FString Input = "typing";
		Input = History.Previous( Input );
		C.Check( Input == "d" && History.IsNavigating(), "Up goes to the latest line" );
		Input = History.Previous( Input );
		Input = History.Previous( Input );
		C.Check( Input == "c", "Up goes back in time" );
		Input = History.Previous( Input );
		C.Check( Input == "c", "and stops at the oldest" );
		Input = History.Next( Input );
		C.Check( Input == "a", "Down goes forward" );
		Input = History.Next( Input );
		Input = History.Next( Input );
		C.Check( Input == "typing" && !History.IsNavigating(), "past the newest line the typed text comes back" );
		C.Check( History.Next( "x" ) == "x", "Down without navigating changes nothing" );
		History.Previous( "q" );
		History.Add( "e" );
		C.Check( !History.IsNavigating(), "adding a line ends the navigation" );

		FConsoleHistory Big( 50 );
		for ( const char* Line : { "teleport 1 2 3", "tel", "scene Game", "teleport 4 5 6", "Teleport 7" } ) Big.Add( Line );
		const TArray<FString> Matches = Big.FindMatches( "tel", 5 );
		C.Check( Matches.Num() == 3 && Matches[0] == "Teleport 7" && Matches[1] == "teleport 4 5 6" && Matches[2] == "teleport 1 2 3", "matching lines come newest first, not case sensitive, without the exact text (%d)", Matches.Num() );
		C.Check( Big.FindMatches( "tel", 2 ).Num() == 2 && Big.FindMatches( "", 5 ).IsEmpty() && Big.FindMatches( "zzz", 5 ).IsEmpty(), "the count is limited and an empty prefix finds nothing" );
		FConsoleHistory Loaded( 3 );
		Loaded.SetAll( TArray<FString> { "1", "2", "3", "4", "2" } );
		C.Check( Loaded.Num() == 3 && Loaded.Get( 0 ) == "3" && Loaded.Get( 1 ) == "4" && Loaded.Get( 2 ) == "2", "SetAll keeps the newest and removes duplicates" );

		C.Group = "console-time";
		FEngineTimeControl Control;
		bool bSame = true;
		for ( const float32 Delta : { 0.0f, 0.016f, 1.0f / 60.0f, 0.5f, 3.14159f } ) bSame = bSame && Control.Apply( Delta ) == Delta;
		C.Check( bSame && Control.GetScale() == 1.0f && !Control.IsPaused(), "by default the delta time passes through unchanged" );
		Control.SetScale( 0.5f );
		C.Near( Control.Apply( 0.02f ), 0.01, 1.0e-7, "scale 0.5 halves the delta time" );
		Control.SetScale( -3.0f );
		C.Check( Control.GetScale() == 0.0f && Control.Apply( 0.02f ) == 0.0f, "a negative scale becomes 0 (frozen)" );
		Control.SetScale( 5000.0f );
		C.Check( Control.GetScale() == FEngineTimeControl::MaxScale, "a huge scale is limited" );
		Control.SetScale( std::nanf( "" ) );
		C.Check( Control.GetScale() == 1.0f, "NaN goes back to 1" );
		Control.SetScale( 2.0f );
		Control.Pause();
		C.Check( Control.IsPaused() && Control.Apply( 0.02f ) == 0.0f && Control.Apply( 0.5f ) == 0.0f, "paused: the delta time is 0" );
		Control.Step( 3 );
		C.Check( Control.GetStepsRemaining() == 3, "Step queues frames" );
		C.Near( Control.Apply( 9.0f ), 2.0 / 60.0, 1.0e-6, "a step is 1/60 s (times the scale)" );
		Control.Apply( 9.0f );
		Control.Apply( 9.0f );
		C.Check( Control.GetStepsRemaining() == 0 && Control.Apply( 9.0f ) == 0.0f && Control.IsPaused(), "after the steps it is paused again" );
		Control.Step( 2 );
		Control.Resume();
		C.Check( !Control.IsPaused() && Control.GetStepsRemaining() == 0 && Control.Apply( 0.1f ) == 0.2f, "Resume cancels the steps" );
		FEngineTimeControl Fresh;
		Fresh.Step( 1 );
		C.Check( Fresh.IsPaused(), "Step on a running game pauses it" );
	}

	//=========================================================================
	// 登録・実行
	//=========================================================================
	void TestRegistry( FChecker& C )
	{
		C.Group = "console-registry";
		FConsoleRegistry Registry;

		int32 Sum = 0;
		FConsoleRegistration Add = Registry.Add( "add", "Adds two numbers", [&Sum]( int32 A, int32 B ) { Sum = A + B; }, { "a", "b" } );
		FRun R = Run( Registry, "add 2 3" );
		C.Check( R.Result == EConsoleResult::Ok && Sum == 5 && R.All.IsEmpty(), "a typed command gets its arguments (%d)", Sum );
		R = Run( Registry, "add 2" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "missing argument 'b'" ) && R.Has( "usage: add <a:int> <b:int>" ), "too few arguments: the missing one and the usage are shown" );
		R = Run( Registry, "add 1 2 3" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "too many arguments" ) && R.Has( "'3'" ), "too many arguments" );
		R = Run( Registry, "add x 1" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "argument 'a': expected an integer, got 'x'" ), "a wrong type names the argument" );
		FConsoleBuffer UpperCaseOutput;
		C.Check( Registry.Execute( "ADD 4 4", UpperCaseOutput ) == EConsoleResult::Ok && Sum == 8 && Registry.Exists( "Add" ), "names are not case sensitive" );

		// 引数なし・省略できる引数・出力・戻り値
		int32 Calls = 0;
		FConsoleRegistration Ping = Registry.Add( "ping", "Does nothing", [&Calls]() { ++Calls; } );
		C.Check( Run( Registry, "ping" ).Result == EConsoleResult::Ok && Calls == 1, "a command without arguments" );
		R = Run( Registry, "ping now" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "takes no arguments" ), "extra arguments to a command without any" );

		TOptional<int32> SeenCount;
		FString SeenName;
		FConsoleRegistration Spawn = Registry.Add( "spawn", "Spawns things", [&]( FConsoleContext& Context, const FString& Name, TOptional<int32> Count )
		{
			SeenName = Name;
			SeenCount = Count;
			Context.Print( "spawned " + Name );
		}, { "name", "count" } );
		R = Run( Registry, "spawn orc" );
		C.Check( R.Result == EConsoleResult::Ok && SeenName == "orc" && !SeenCount.IsSet() && R.Has( "spawned orc" ), "an optional argument can be left out; the context prints" );
		R = Run( Registry, "spawn orc 4" );
		C.Check( R.Result == EConsoleResult::Ok && SeenCount.IsSet() && SeenCount.GetValue() == 4, "or given" );
		R = Run( Registry, "spawn orc many" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "argument 'count'" ), "and checked when given" );
		C.Check( Registry.Find( "spawn" ) != nullptr && Registry.Find( "spawn" )->Args[1].bOptional && !Registry.Find( "spawn" )->Args[0].bOptional && Registry.Find( "spawn" )->GetUsage() == "spawn <name:text> [count:int]",
				 "the usage shows optional arguments in brackets (%s)", Registry.Find( "spawn" ) != nullptr ? *Registry.Find( "spawn" )->GetUsage() : "?" );

		FConsoleRegistration Text = Registry.Add( "text", "Returns text", []( int32 N ) { return FString::Printf( "n=%d", N ); }, { "n" } );
		R = Run( Registry, "text 7" );
		C.Check( R.Result == EConsoleResult::Ok && R.All == "n=7", "a returned FString is printed" );
		FConsoleRegistration Fail = Registry.Add( "fail", "Fails", []() { return false; } );
		C.Check( Run( Registry, "fail" ).Result == EConsoleResult::Failed, "returning false is a failure" );
		FConsoleRegistration Err = Registry.Add( "err", "Fails with a reason", []() { return TResult<void>::Err( "the reason" ); } );
		R = Run( Registry, "err" );
		C.Check( R.Result == EConsoleResult::Failed && R.Has( "the reason" ) && R.HasError(), "a TResult error is printed as an error" );
		FConsoleRegistration Good = Registry.Add( "good", "Succeeds", []() { return TResult<void>::Ok(); } );
		C.Check( Run( Registry, "good" ).Result == EConsoleResult::Ok, "a TResult ok is a success" );

		FVector3 Moved;
		FConsoleRegistration Move = Registry.Add( "move", "Moves", [&Moved]( const FVector3& Position, TOptional<float32> Speed ) { Moved = Position * Speed.Get( 1.0f ); }, { "position", "speed" } );
		C.Check( Run( Registry, "move 1 2 3" ).Result == EConsoleResult::Ok && Moved.x == 1.0f && Moved.z == 3.0f, "a vec3 argument takes three words" );
		C.Check( Run( Registry, "move 1 2 3 2" ).Result == EConsoleResult::Ok && Moved.y == 4.0f, "and the next argument follows it" );
		R = Run( Registry, "move 1 2" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.Has( "needs 3 values" ), "too few numbers for a vec3" );

		FString Said;
		FConsoleRegistration Say = Registry.Add( "say", "Says", [&Said]( const FConsoleRest& Words ) { Said = Words.Text; } );
		Run( Registry, "say hello  big \"wide  world\"" );
		C.Check( Said == "hello big wide  world", "a rest argument takes all the remaining words (%s)", *Said );
		C.Check( Run( Registry, "say" ).Result == EConsoleResult::Ok && Said.IsEmpty(), "and may be empty" );

		// 自分で引数を読む
		TArray<FString> RawArgs;
		FConsoleRegistration Raw = Registry.AddRaw( "raw", "Reads its own arguments", [&RawArgs]( FConsoleContext& Context ) { RawArgs = Context.GetArgs(); return EConsoleResult::Ok; } );
		Run( Registry, "raw a \"b c\" d" );
		C.Check( RawArgs.Num() == 3 && RawArgs[1] == "b c", "a raw command reads Context.GetArgs()" );

		// 説明・登録の解除・置き換え
		FConsoleRegistration Give = Registry.Add( "give", "Gives an item", []( const FString&, TOptional<int32> ) {}, { { "item", "Item id" }, "count" }, "Cheats" );
		const FConsoleCommandInfo* GiveInfo = Registry.Find( "give" );
		C.Check( GiveInfo != nullptr && GiveInfo->Description == "Gives an item" && GiveInfo->Category == "Cheats" && GiveInfo->Args[0].Description == "Item id" && GiveInfo->Args[1].Description.IsEmpty() && GiveInfo->Args[1].Name == "count",
				 "a description can be given to the command and to each argument" );
		const FConsoleCommandInfo* AddInfo = Registry.Find( "add" );
		C.Check( AddInfo != nullptr && AddInfo->Args[0].Description.IsEmpty() && AddInfo->Args[0].Name == "a", "arguments without a description have none" );
		FConsoleRegistration Unnamed = Registry.Add( "unnamed", "No argument names", []( int32, float32 ) {} );
		C.Check( Registry.Find( "unnamed" )->Args[0].Name == "arg1" && Registry.Find( "unnamed" )->Args[1].Name == "arg2", "missing argument names become arg1, arg2" );

		{
			FConsoleRegistration Temporary = Registry.Add( "temp", "Temporary", []() {} );
			C.Check( Registry.Exists( "temp" ) && Temporary.IsRegistered(), "a registration is alive while the handle is" );
		}
		C.Check( !Registry.Exists( "temp" ), "dropping the handle removes the command" );
		{
			FConsoleRegistration Kept = Registry.Add( "kept", "Kept", []() {} );
			Kept.Keep();
		}
		C.Check( Registry.Exists( "kept" ), "Keep() leaves the command registered" );
		int32 Version = 0;
		FConsoleRegistration Old = Registry.Add( "replaced", "Version 1", [&Version]() { Version = 1; } );
		FConsoleRegistration New = Registry.Add( "replaced", "Version 2", [&Version]() { Version = 2; } );
		Run( Registry, "replaced" );
		C.Check( Version == 2 && Registry.Find( "replaced" )->Description == "Version 2", "registering a name again replaces the command" );
		Old.Unregister();
		C.Check( Registry.Exists( "replaced" ), "an old handle does not remove the command that replaced it" );
		C.Check( Registry.Remove( "replaced" ) && !Registry.Exists( "replaced" ) && !Registry.Remove( "replaced" ), "Remove" );

		// 複数・空・注釈・未知
		int32 Order = 0, FirstAt = 0, SecondAt = 0;
		FConsoleRegistration First = Registry.Add( "first", "First", [&]() { FirstAt = ++Order; } );
		FConsoleRegistration Second = Registry.Add( "second", "Second", [&]() { SecondAt = ++Order; } );
		C.Check( Run( Registry, "first; second" ).Result == EConsoleResult::Ok && FirstAt == 1 && SecondAt == 2, "several commands run in order" );
		SecondAt = 0;
		R = Run( Registry, "nosuch; second" );
		C.Check( R.Result == EConsoleResult::UnknownCommand && SecondAt != 0, "a failure does not stop the next command, and is the result" );
		C.Check( Run( Registry, "" ).Result == EConsoleResult::Empty && Run( Registry, "# note" ).Result == EConsoleResult::Empty && Run( Registry, "  " ).Result == EConsoleResult::Empty, "empty lines and comments do nothing" );
		R = Run( Registry, "ad 1 2" );
		C.Check( R.Result == EConsoleResult::UnknownCommand && R.Has( "unknown command 'ad'" ) && R.Has( "Did you mean: add" ), "an unknown command suggests similar ones" );
		R = Run( Registry, "qqqqqqqq" );
		C.Check( R.Has( "help" ) && !R.Has( "Did you mean" ), "with nothing similar it points to help" );
		R = Run( Registry, "sai hi" );
		C.Check( R.Has( "Did you mean: say" ), "a typo (one letter off) suggests the command (%s)", *R.All );
		R = Run( Registry, "say \"unclosed" );
		C.Check( R.Result == EConsoleResult::BadArguments && R.HasError(), "a syntax error is reported" );

		// 別名
		FString AliasError;
		C.Check( Registry.SetAlias( "plus1", "add 1", AliasError ), "an alias can be defined" );
		Sum = 0;
		C.Check( Run( Registry, "plus1 5" ).Result == EConsoleResult::Ok && Sum == 6, "arguments after an alias are added to its line" );
		C.Check( Registry.SetAlias( "both", "first; second", AliasError ), "an alias may hold several commands" );
		Order = 0;
		Run( Registry, "both" );
		C.Check( FirstAt == 1 && SecondAt == 2, "and runs them all" );
		C.Check( Registry.SetAlias( "quoted", "say", AliasError ) && Run( Registry, "quoted \"a  b\"" ).Result == EConsoleResult::Ok && Said == "a  b", "quotes survive the trip through an alias" );
		C.Check( !Registry.SetAlias( "add", "first", AliasError ) && AliasError.Contains( "already a command" ), "an alias cannot hide a command" );
		C.Check( !Registry.SetAlias( "a b", "first", AliasError ) && !Registry.SetAlias( "", "first", AliasError ) && !Registry.SetAlias( "x", "  ", AliasError ), "bad alias names or empty lines are refused" );
		C.Check( Registry.SetAlias( "loop", "loop", AliasError ), "a self-calling alias can be defined" );
		R = Run( Registry, "loop" );
		C.Check( R.Result == EConsoleResult::Failed && R.Has( "calls itself too deeply" ), "but is stopped (%s)", *R.All );
		C.Check( Registry.FindAlias( "PLUS1" ) != nullptr && *Registry.FindAlias( "plus1" ) == "add 1" && Registry.GetAliases().Num() == 4, "aliases are found by name" );
		FConsoleRegistration Shadow = Registry.Add( "plus1", "Now a command", []() {} );
		C.Check( Registry.FindAlias( "plus1" ) == nullptr, "registering a command removes an alias with the same name" );
		C.Check( Registry.RemoveAlias( "both" ) && !Registry.RemoveAlias( "both" ), "aliases can be removed" );

		// 変数
		int32 Speed = 1, Changed = -1;
		TConsoleVariableOptions<int32> SpeedOptions;
		SpeedOptions.Min = 0;
		SpeedOptions.Max = 10;
		SpeedOptions.OnChanged = [&Changed]( const int32& NewValue ) { Changed = NewValue; };
		FConsoleRegistration SpeedVar = Registry.AddVariable( "speed", &Speed, "Speed of things", SpeedOptions );
		R = Run( Registry, "speed" );
		C.Check( R.Result == EConsoleResult::Ok && R.All == "speed = 1", "a variable shows its value (%s)", *R.All );
		R = Run( Registry, "speed 5" );
		C.Check( Speed == 5 && Changed == 5 && R.All == "speed = 5", "and is set by name and value" );
		Run( Registry, "speed 99" );
		C.Check( Speed == 10, "values above the maximum are limited" );
		Run( Registry, "speed -4" );
		C.Check( Speed == 0, "values below the minimum are limited" );
		R = Run( Registry, "speed abc" );
		C.Check( R.Result == EConsoleResult::BadArguments && Speed == 0, "a bad value changes nothing" );
		FString Text2;
		C.Check( Registry.GetVariableText( "speed", Text2 ) && Text2 == "0" && Registry.Find( "speed" )->bVariable, "GetVariableText and the variable flag" );
		C.Check( Changed == 0, "OnChanged got the limited value (%d)", Changed );
		C.Check( Registry.ResetVariable( "speed" ) && Speed == 1, "ResetVariable puts the registered value back" );
		C.Check( Changed == 0, "and does not call OnChanged" );
		C.Check( !Registry.ResetVariable( "add" ) && !Registry.GetVariableText( "add", Text2 ), "ordinary commands are not variables" );

		bool bFlag = false;
		FConsoleRegistration FlagVar = Registry.AddVariable( "flag", &bFlag, "A switch" );
		Run( Registry, "flag on" );
		C.Check( bFlag && Run( Registry, "flag" ).All == "flag = true", "a bool variable" );
		C.Check( Run( Registry, "flag maybe" ).Result == EConsoleResult::BadArguments && bFlag, "and refuses a bad word" );
		FVector3 Position( 1, 2, 3 );
		FConsoleRegistration PositionVar = Registry.AddVariable( "pos", &Position, "A position" );
		Run( Registry, "pos 4 5 6" );
		C.Check( Position.y == 5.0f && Run( Registry, "pos" ).All == "pos = 4 5 6", "a vec3 variable" );
		FString Name2 = "start";
		FConsoleRegistration NameVar = Registry.AddVariable( "title", &Name2, "A text" );
		Run( Registry, "title \"two words\"" );
		C.Check( Name2 == "two words", "a text variable" );
		int32 Store = 0;
		FConsoleRegistration GetSet = Registry.AddVariable<int32>( "doubled", [&Store]() { return Store; }, [&Store]( const int32& NewValue ) { Store = NewValue * 2; }, "Set doubles it" );
		Run( Registry, "doubled 4" );
		C.Check( Store == 8 && Run( Registry, "doubled" ).All == "doubled = 8", "a variable can use a getter and a setter" );
		Run( Registry, "doubled 1" );
		Registry.ResetVariable( "doubled" );
		C.Check( Store == 0, "and resets through the setter (the value at registration was 0)" );

		// 一覧
		FConsoleRegistration Secret = Registry.Add( "secret", "Hidden one", []() {} );
		Registry.SetHidden( "secret", true );
		bool bSecretListed = false, bSecretAll = false;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() ) bSecretListed = bSecretListed || Info->Name == "secret";
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands( true ) ) bSecretAll = bSecretAll || Info->Name == "secret";
		C.Check( !bSecretListed && bSecretAll && Run( Registry, "secret" ).Result == EConsoleResult::Ok, "hidden commands are not listed, but still run" );
		const TArray<const FConsoleCommandInfo*> List = Registry.GetCommands();
		bool bSorted = true;
		for ( int32 i = 1; i < List.Num(); ++i )
		{
			const FString GroupA = List[i - 1]->Category.IsEmpty() ? FString( "general" ) : List[i - 1]->Category.ToLower();
			const FString GroupB = List[i]->Category.IsEmpty() ? FString( "general" ) : List[i]->Category.ToLower();
			if ( GroupA > GroupB || ( GroupA == GroupB && List[i - 1]->Name.ToLower() > List[i]->Name.ToLower() ) ) bSorted = false;
		}
		C.Check( bSorted && List.Num() > 10, "the list is sorted by category and name" );
	}

	//=========================================================================
	// 補完
	//=========================================================================
	void TestCompletion( FChecker& C )
	{
		C.Group = "console-completion";
		FConsoleRegistry Registry;
		FConsoleRegistration Add = Registry.Add( "add", "Adds two numbers", []( int32, int32 ) {}, { { "a", "The first number" }, "b" } );
		FConsoleRegistration Address = Registry.Add( "address", "Shows an address", []() {} );
		FConsoleRegistration Alpha = Registry.Add( "foo.alpha", "Alpha", []() {} );
		FConsoleRegistration Beta = Registry.Add( "foo.beta", "Beta", []() {} );
		bool bFlag = false;
		FConsoleRegistration Flag = Registry.AddVariable( "flag", &bFlag, "A switch" );
		FConsoleRegistration Spawn = Registry.Add( "spawn", "Spawns", []( const FString&, TOptional<int32> ) {}, { "name", "count" } );
		Registry.SetArgCompleter( "spawn", 0, []() { return TArray<FString> { "my scene", "other", "my other" }; } );
		FConsoleRegistration Move = Registry.Add( "move", "Moves", []( const FVector3&, float32 ) {}, { "position", "speed" } );
		FConsoleRegistration Say = Registry.Add( "say", "Says", []( const FConsoleRest& ) {}, { "words" } );
		Registry.SetArgChoices( "say", 0, { "hello", "help" } );
		FString Error;
		Registry.SetAlias( "adder", "add 1", Error );
		FConsoleRegistration Secret = Registry.Add( "addsecret", "Hidden", []() {} );
		Registry.SetHidden( "addsecret", true );

		FConsoleCompletion Completion = Registry.Complete( "ad" );
		C.Check( Completion.ReplaceFrom == 0 && Completion.Candidates.Num() == 3 && Completion.Candidates[0] == "add" && Completion.Candidates[1] == "adder" && Completion.Candidates[2] == "address", "command names (and aliases) with the typed start, sorted; hidden ones excluded (%d)",
				 Completion.Candidates.Num() );
		C.Check( Completion.Descriptions.Num() == 3 && Completion.Descriptions[0] == "Adds two numbers" && Completion.Descriptions[1] == "alias: add 1" && Completion.Descriptions[2] == "Shows an address", "each candidate comes with its description" );
		C.Check( Completion.CommonPrefix == "add", "the common prefix of the candidates" );
		C.Check( Registry.Complete( "AD" ).Candidates.Num() == 3, "the start is not case sensitive" );
		Completion = Registry.Complete( "foo." );
		C.Check( Completion.Candidates.Num() == 2 && Completion.CommonPrefix == "foo.", "the common prefix stops where the candidates differ" );
		Completion = Registry.Complete( "address" );
		C.Check( Completion.Candidates.Num() == 1 && Completion.Candidates[0] == "address" && Completion.CommonPrefix == "address", "a single candidate is its own common prefix" );
		C.Check( Registry.Complete( "zzz" ).Candidates.IsEmpty() && Registry.Complete( "zzz" ).CommonPrefix.IsEmpty(), "no candidates, no prefix" );
		C.Check( Registry.Complete( "" ).Candidates.Num() > 5, "an empty line completes to every command" );

		Completion = Registry.Complete( "add" );
		C.Check( Completion.Command != nullptr && Completion.Command->Name == "add" && Completion.Usage == "add <a:int> <b:int>", "once the name is complete the command and its usage are known" );
		Completion = Registry.Complete( "add " );
		C.Check( Completion.Command != nullptr && Completion.ActiveArg == 0 && Completion.Usage == "add <a:int> <b:int>" && Completion.Candidates.IsEmpty(), "after the name the first argument is active" );
		C.Check( Completion.Command != nullptr && Completion.Command->Args[0].Description == "The first number" && Completion.Command->Args[1].Description.IsEmpty(), "the argument descriptions are reachable for display" );
		C.Check( Registry.Complete( "add 1 " ).ActiveArg == 1 && Registry.Complete( "add 1 2 " ).ActiveArg == INDEX_NONE, "the active argument moves along (and ends after the last)" );
		C.Check( Registry.Complete( "adder " ).Command == nullptr, "an alias has no usage" );

		Completion = Registry.Complete( "flag " );
		C.Check( Completion.Candidates.Num() == 4 && Completion.Candidates[0] == "false" && Completion.Candidates[2] == "on" && Completion.Candidates[3] == "true", "a bool argument offers true / false / on / off" );
		Completion = Registry.Complete( "flag t" );
		C.Check( Completion.Candidates.Num() == 1 && Completion.Candidates[0] == "true" && Completion.ReplaceFrom == 5, "and filters by what was typed" );

		Completion = Registry.Complete( "spawn " );
		C.Check( Completion.Candidates.Num() == 3 && Completion.Candidates[0] == "\"my other\"" && Completion.Candidates[1] == "\"my scene\"" && Completion.Candidates[2] == "other", "candidates with spaces are quoted (computed when asked)" );
		Completion = Registry.Complete( "spawn \"my s" );
		C.Check( Completion.Candidates.Num() == 1 && Completion.Candidates[0] == "\"my scene\"" && Completion.ReplaceFrom == 6, "inside an open quote the whole quoted word is replaced" );
		C.Check( Registry.Complete( "spawn other " ).ActiveArg == 1 && Registry.Complete( "spawn other " ).Candidates.IsEmpty(), "the second argument has no candidates here" );

		C.Check( Registry.Complete( "move " ).ActiveArg == 0 && Registry.Complete( "move 1 " ).ActiveArg == 0 && Registry.Complete( "move 1 2 " ).ActiveArg == 0 && Registry.Complete( "move 1 2 3 " ).ActiveArg == 1,
				 "a vec3 argument stays active for its three words" );
		C.Check( Registry.Complete( "say a b c " ).ActiveArg == 0, "a rest argument stays active to the end" );
		Completion = Registry.Complete( "say hel" );
		C.Check( Completion.Candidates.Num() == 2 && Completion.CommonPrefix == "hel", "choices can be set afterwards" );

		Completion = Registry.Complete( "say hi; ad" );
		C.Check( Completion.ReplaceFrom == 8 && Completion.Candidates.Num() == 3, "after a ';' the next command is completed" );
	}

	//=========================================================================
	// 起動時の登録
	//=========================================================================
	int32 GMacroVariable = 5;
	bool GRegistrarRan = false;
	const NConsole::FStaticRegistrar GAlwaysRegistered( [] { GRegistrarRan = true; } );

	CONSOLE_COMMAND( "selftest.macro", "A command registered by the macro", []( FConsoleContext& Context ) { Context.Print( "macro ok" ); } );
	CONSOLE_VARIABLE( "selftest.macrovar", &GMacroVariable, "A variable registered by the macro" );

	void TestStaticRegistration( FChecker& C )
	{
		C.Group = "console-static";
		C.Check( GRegistrarRan, "a static registrar runs before main" );
#if COG_DEBUG_CONSOLE
		FConsoleRegistry& Global = FConsoleRegistry::Global();
		C.Check( Global.Exists( "selftest.macro" ) && Global.Exists( "selftest.macrovar" ), "CONSOLE_COMMAND / CONSOLE_VARIABLE register in the global registry (debug console is on)" );
		FRun R = Run( Global, "selftest.macro" );
		C.Check( R.Result == EConsoleResult::Ok && R.All == "macro ok", "the macro's command runs" );
		Run( Global, "selftest.macrovar 9" );
		C.Check( GMacroVariable == 9, "the macro's variable is set" );
		GMacroVariable = 5;
#else
		C.Check( !FConsoleRegistry::Global().Exists( "selftest.macro" ) && !FConsoleRegistry::Global().Exists( "selftest.macrovar" ), "the macros do nothing when the debug console is off" );
#endif
	}

	//=========================================================================
	// 組み込みのコマンド（動いているアプリに対して。シーンのものは RunConsoleSceneChecks）
	//=========================================================================
	void TestBuiltins( FChecker& C )
	{
		C.Group = "console-builtin";
		FConsoleRegistry Registry;
		NConsole::RegisterBuiltinCommands( Registry );

		// すべてに説明と分類がある
		const TArray<const FConsoleCommandInfo*> All = Registry.GetCommands( true );
		bool bAllDescribed = true, bAllCategorised = true;
		FString First;
		for ( const FConsoleCommandInfo* Info : All )
		{
			if ( Info->Description.TrimStartAndEnd().IsEmpty() ) { bAllDescribed = false; if ( First.IsEmpty() ) First = Info->Name; }
			if ( Info->Category.IsEmpty() ) bAllCategorised = false;
		}
		C.Check( bAllDescribed, "every built-in command has a description (first without: '%s')", *First );
		C.Check( bAllCategorised, "and a category" );
		C.Check( All.Num() >= 55, "there are plenty of built-in commands (%d)", All.Num() );
		bool bKnown = true;
		const char* Categories[] = { "Console", "Variables", "Engine", "Window", "Scene", "Objects", "ECS", "Assets", "Audio", "Profiler", "Log", "Info" };
		for ( const FConsoleCommandInfo* Info : All )
		{
			bool bFound = false;
			for ( const char* Category : Categories ) bFound = bFound || Info->Category == Category;
			bKnown = bKnown && bFound;
		}
		C.Check( bKnown, "each category is one of the known groups" );
		for ( const char* Name : { "help", "find", "echo", "clear", "history", "alias", "unalias", "exec", "repeat", "quit", "exit", "vars", "set", "toggle", "reset", "fps", "time", "pause", "resume", "step", "jobs", "memory",
								   "screenshot", "window", "windowmode", "fullscreen", "scene", "scenes", "push", "pop", "reload", "objects", "object", "destroy", "visible", "tick", "teleport", "ecs", "systems", "assets",
								   "assets.list", "assets.dump", "assets.release", "assets.root", "assets.export", "audio", "audio.voices", "audio.volume", "audio.mute", "audio.pause", "audio.stop", "audio.play", "profiler",
								   "profiler.capture", "profiler.clear", "profiler.stats", "profiler.save", "log", "version", "cmdline", "cwd", "date", "timescale", "window.title", "cursor.visible", "audio.master" } )
		{
			if ( !Registry.Exists( Name ) ) C.Check( false, "the built-in command '%s' exists", Name );
		}
		// 引数の説明がある引数は、名前と説明が空でない
		const FConsoleCommandInfo* SceneInfo = Registry.Find( "scene" );
		C.Check( SceneInfo != nullptr && !SceneInfo->Args[0].Description.IsEmpty() && SceneInfo->Args[0].bOptional, "built-in arguments carry descriptions where they help" );

		// help
		FRun R = Run( Registry, "help" );
		C.Check( R.Result == EConsoleResult::Ok && R.Has( "[Console]" ) && R.Has( "[Audio]" ) && R.CountLines( "help" ) >= 1 && R.Has( "Show the list of commands" ), "help lists the commands by category with their descriptions" );
		R = Run( Registry, "help scene" );
		C.Check( R.Result == EConsoleResult::Ok && R.Has( "usage: scene [name:text]" ) && R.Has( "A registered scene name" ) && R.Has( "(text, optional)" ), "help <command> shows the usage and the described arguments" );
		R = Run( Registry, "help fps" );
		C.Check( R.Has( "usage: fps" ) && R.CountLines( "(" ) <= 1, "and no argument lines for a command without arguments" );
		R = Run( Registry, "help timescale" );
		C.Check( R.Has( "current value: 1" ), "help on a variable shows its value" );
		C.Check( Run( Registry, "help nosuchthing" ).Result == EConsoleResult::Failed, "help on an unknown command fails" );
		R = Run( Registry, "find audio" );
		C.Check( R.Has( "audio.volume" ) && !R.Has( "scenes" ), "find searches names and descriptions" );
		C.Check( Run( Registry, "find qqqqqq" ).Has( "no command matches" ), "find says when nothing matches" );

		// 出力・文字
		C.Check( Run( Registry, "echo hello   world" ).All == "hello world" && Run( Registry, "echo \"a  b\" c" ).All == "a  b c", "echo prints its words" );
		FConsoleBuffer ClearTarget;
		ClearTarget.Write( EConsoleLine::Info, "x" );
		Registry.Execute( "clear", ClearTarget );
		C.Check( ClearTarget.Num() == 0, "clear empties the output" );

		// 別名・繰り返し
		C.Check( Run( Registry, "alias hi echo hello there" ).Has( "alias hi = echo hello there" ) && Run( Registry, "hi you" ).All == "hello there you", "alias defines a short name; its arguments are added" );
		C.Check( Run( Registry, "alias hi" ).All == "hi = echo hello there" && Run( Registry, "alias" ).Has( "hi" ), "alias shows one alias, or lists them" );
		C.Check( Run( Registry, "unalias hi" ).Result == EConsoleResult::Ok && Run( Registry, "unalias hi" ).Result == EConsoleResult::Failed, "unalias removes it" );
		C.Check( Run( Registry, "alias echo2 echo" ).Result == EConsoleResult::Ok && Run( Registry, "alias echo hi" ).Result == EConsoleResult::Failed, "alias refuses the name of a command" );
		FRun Repeated = Run( Registry, "repeat 3 echo x" );
		C.Check( Repeated.Result == EConsoleResult::Ok && Repeated.CountLines( "x" ) == 3, "repeat runs a line several times" );
		C.Check( Run( Registry, "repeat 2 echo \"a  b\"" ).CountLines( "a  b" ) == 2, "and keeps the quoting of the line" );
		C.Check( Run( Registry, "repeat 0 echo x" ).Result == EConsoleResult::Failed && Run( Registry, "repeat 5000 echo x" ).Result == EConsoleResult::Failed && Run( Registry, "repeat 3" ).Result == EConsoleResult::Failed, "repeat checks its arguments" );

		// 履歴
		FConsoleHistory& History = NConsole::GetGlobalHistory();
		const TArray<FString> SavedHistory = History.GetAll();
		History.Clear();
		C.Check( Run( Registry, "history" ).Has( "no history yet" ), "history with nothing yet" );
		History.Add( "fps" );
		History.Add( "scene" );
		R = Run( Registry, "history 5" );
		C.Check( R.Has( "fps" ) && R.Has( "scene" ), "history shows the entered lines" );
		History.SetAll( SavedHistory );

		// スクリプト
		const FString Dir = MakeTempDirectory() / "console";
		CreateDirectoryA( *Dir, nullptr );
		WriteFileText( Dir / "ok.cfg", "# a comment\n\necho first\n// another comment\necho second; echo third\nnosuchcommand 1\necho after the failure\n" );
		R = Run( Registry, "exec \"" + Dir / "ok.cfg" + "\"" );
		C.Check( R.Result == EConsoleResult::Failed && R.Has( "first" ) && R.Has( "third" ) && R.Has( "after the failure" ) && R.Has( "ran 4 command(s)" ) && R.Has( "1 failed" ) && R.Has( "line 6" ),
				 "exec runs every line, skips comments, runs on after a failure and reports it (%s)", *R.All );
		CreateDirectoryA( *( Dir / "sub" ), nullptr );
		WriteFileText( Dir / "sub" / "outer.cfg", "echo outer ran\nexec inner.cfg\n" );
		WriteFileText( Dir / "sub" / "inner.cfg", "echo inner ran\n" );
		R = Run( Registry, "exec \"" + Dir / "sub" / "outer.cfg" + "\"" );
		C.Check( R.Result == EConsoleResult::Ok && R.Has( "outer ran" ) && R.Has( "inner ran" ), "a script's relative exec is found next to that script (%s)", *R.All );
		WriteFileText( Dir / "loop.cfg", "exec loop.cfg\n" );
		FConsoleBuffer LoopOutput;
		FString ScriptError;
		int32 NumCommands = 0, NumFailed = 0;
		const bool bLoopRead = NConsole::ExecuteScriptFile( Registry, Dir / "loop.cfg", LoopOutput, ScriptError, &NumCommands, &NumFailed );
		C.Check( bLoopRead && LoopOutput.ToText().Contains( "nested too deeply" ), "a script that runs itself is stopped" );
		R = Run( Registry, "exec nosuchfile.cfg" );
		C.Check( R.Result == EConsoleResult::Failed && R.Has( "file not found" ), "exec of a missing file fails with a reason" );
		WriteFileText( Dir / "utf8.cfg", "\xEF\xBB\xBF" "echo \xE8\xA1\xA8\n" );   // BOM つきの UTF-8 で「表」
		FConsoleBuffer Utf8Output;
		NConsole::ExecuteScriptFile( Registry, Dir / "utf8.cfg", Utf8Output, ScriptError );
		C.Check( Utf8Output.ToText() == "\x95\\", "a UTF-8 script is read into the system code page" );
		C.Check( NConsole::FindScriptFile( Dir / "ok.cfg" ) == Dir / "ok.cfg" && NConsole::FindScriptFile( "no-such-script-xyz.cfg" ).IsEmpty(), "FindScriptFile finds a path as given, and nothing for a missing one" );
		RemoveDirectoryRecursive( Dir );

		// 変数
		const FString Vars = Run( Registry, "vars" ).All;
		C.Check( Vars.Contains( "timescale" ) && Vars.Contains( "cursor.visible" ) && Vars.Contains( "audio.master" ) && Vars.Contains( "window.title" ), "vars lists the variables with their descriptions" );
		C.Check( !Run( Registry, "vars audio" ).Has( "timescale" ) && Run( Registry, "vars audio" ).Has( "audio.master" ), "vars can filter" );
		CEngine* Engine = CEngine::Get();
		if ( Engine != nullptr )
		{
			FEngineTimeControl& Control = Engine->GetTimeControl();
			const float32 OldScale = Control.GetScale();
			C.Check( Run( Registry, "timescale" ).All == "timescale = " + NConsole::FormatFloat( OldScale ), "timescale shows the engine's value" );
			Run( Registry, "timescale 0.5" );
			C.Check( Control.GetScale() == 0.5f, "and sets it" );
			Run( Registry, "set timescale 2" );
			C.Check( Control.GetScale() == 2.0f, "set does the same" );
			Run( Registry, "timescale 9999" );
			C.Check( Control.GetScale() == FEngineTimeControl::MaxScale, "and the value is limited" );
			Run( Registry, "reset timescale" );
			C.Check( Control.GetScale() == 1.0f, "reset puts it back" );
			Control.SetScale( OldScale );

			const bool bWasPaused = Control.IsPaused();
			Run( Registry, "pause" );
			C.Check( Control.IsPaused() != bWasPaused, "pause switches" );
			Run( Registry, "pause off" );
			C.Check( !Control.IsPaused(), "pause off resumes" );
			Run( Registry, "step 3" );
			C.Check( Control.IsPaused() && Control.GetStepsRemaining() == 3, "step pauses and queues frames" );
			Run( Registry, "resume" );
			C.Check( !Control.IsPaused() && Control.GetStepsRemaining() == 0, "resume continues" );
			if ( bWasPaused ) Control.Pause();
			C.Check( Run( Registry, "time" ).Has( "timescale 1" ) && Run( Registry, "time" ).Has( "running" ), "time shows the scale and the state" );
		}
		IPlatform* Platform = GetAppSubsystem<IPlatform>();
		if ( Platform != nullptr )
		{
			C.Check( Run( Registry, "fps" ).Has( "FPS" ) && Run( Registry, "fps" ).Has( "frame" ), "fps" );
			C.Check( Run( Registry, "window" ).Has( "x" ) && Run( Registry, "window" ).Has( "title" ) && Run( Registry, "windowmode" ).Result == EConsoleResult::Ok, "window info and mode" );
			C.Check( Run( Registry, "windowmode sideways" ).Result == EConsoleResult::Failed, "an unknown window mode is refused" );
			const bool bCursor = Platform->IsMouseCursorVisible();
			Run( Registry, "toggle cursor.visible" );
			C.Check( Platform->IsMouseCursorVisible() != bCursor, "toggle flips a bool variable" );
			Run( Registry, "cursor.visible " + FString( bCursor ? "true" : "false" ) );
			C.Check( Platform->IsMouseCursorVisible() == bCursor, "and a variable can be set back by name" );
			C.Check( Run( Registry, "toggle timescale" ).Result == EConsoleResult::Failed && Run( Registry, "set nosuch 1" ).Result == EConsoleResult::Failed && Run( Registry, "reset help" ).Result == EConsoleResult::Failed,
					 "toggle / set / reset refuse what is not a fitting variable" );
		}
		C.Check( Run( Registry, "jobs" ).Has( "worker" ) && Run( Registry, "memory" ).Has( "process" ), "jobs and memory" );
		FRun Info = Run( Registry, "version" );
		C.Check( Info.Has( "build" ) && Info.Has( "MSVC" ) && Run( Registry, "cmdline" ).All.Len() > 0 && Run( Registry, "cwd" ).Has( "project root" ), "version, cmdline, cwd" );
		const FString Date = Run( Registry, "date" ).All;
		C.Check( Date.Len() == 19 && Date.StartsWith( "20" ) && Date[4] == '-', "date has the form 2026-10-18 12:34:56 (%s)", *Date );

		// ログ
		FLogCategory* Category = CLogManager::Get().FindCategory( "LogAssetSelfTest" );
		C.Check( Category != nullptr && Run( Registry, "log" ).Has( "LogAssetSelfTest" ), "log lists the categories" );
		if ( Category != nullptr )
		{
			const ELogVerbosity OldLevel = Category->GetVerbosity();
			Run( Registry, "log logassetselftest verbose" );
			C.Check( Category->GetVerbosity() == ELogVerbosity::Verbose, "log <category> <level> sets the level (category names are not case sensitive)" );
			C.Check( Run( Registry, "log LogAssetSelfTest" ).Has( "Verbose" ), "log <category> shows it" );
			Category->SetVerbosity( OldLevel );
		}
		C.Check( Run( Registry, "log LogNoSuchCategory" ).Result == EConsoleResult::Failed && Run( Registry, "log LogAssetSelfTest loudly" ).Result == EConsoleResult::Failed && Run( Registry, "log all" ).Result == EConsoleResult::Failed,
				 "log refuses unknown categories and levels, and 'all' without a level" );

		// アセット・オーディオ・プロファイラー
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		if ( Assets != nullptr )
		{
			C.Check( Run( Registry, "assets" ).Has( "asset(s)" ) && Run( Registry, "assets" ).Has( "Texture" ) && Run( Registry, "assets.list" ).Has( "asset(s)" ), "assets and assets.list" );
			C.Check( Run( Registry, "assets.root" ).All == Assets->GetRootDirectory() && Run( Registry, "assets.release" ).Result == EConsoleResult::Ok, "assets.root and assets.release" );
		}
		CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
		if ( Audio != nullptr )
		{
			const float32 OldVolume = Audio->GetBusVolume( EAudioBus::Sfx );
			Run( Registry, "audio.volume sfx 0.25" );
			C.Near( Audio->GetBusVolume( EAudioBus::Sfx ), 0.25, 1.0e-4, "audio.volume sets a bus volume (bus names are not case sensitive)" );
			C.Check( Run( Registry, "audio.volume Sfx" ).Has( "Sfx volume 0.25" ), "and shows it" );
			Audio->SetBusVolume( EAudioBus::Sfx, OldVolume );
			const bool bMuted = Audio->IsBusMuted( EAudioBus::Ui );
			Run( Registry, "audio.mute ui" );
			C.Check( Audio->IsBusMuted( EAudioBus::Ui ) != bMuted, "audio.mute switches a bus" );
			Audio->SetBusMuted( EAudioBus::Ui, bMuted );
			R = Run( Registry, "audio.volume nobus 1" );
			C.Check( R.Result == EConsoleResult::Failed && R.Has( "Master, Music, Sfx" ), "an unknown bus lists the buses" );
			C.Check( Run( Registry, "audio" ).Has( "voice(s)" ) && Run( Registry, "audio.voices" ).Has( "voice(s)" ), "audio and audio.voices" );
			C.Check( Registry.Find( "audio.volume" ) != nullptr && Registry.Find( "audio.volume" )->Args[0].Choices.Num() == 6, "the bus argument offers the six buses" );
		}
		C.Check( Run( Registry, "profiler" ).Has( "profiler:" ) && Run( Registry, "profiler.stats" ).Has( "frame(s)" ) && Run( Registry, "profiler.capture 0" ).Result == EConsoleResult::Failed, "profiler commands" );
		C.Check( Run( Registry, "scenes" ).Result != EConsoleResult::UnknownCommand && Run( Registry, "scene nosuchscene" ).Result == EConsoleResult::Failed, "scene commands exist and refuse an unknown scene" );

		// 補完・引数の候補（組み込み）
		C.Check( Registry.Complete( "wi" ).Candidates.Num() == 3, "window, windowmode and window.title complete from 'wi' (%d)", Registry.Complete( "wi" ).Candidates.Num() );
		C.Check( Registry.Complete( "windowmode b" ).Candidates.Num() == 1 && Registry.Complete( "windowmode b" ).Candidates[0] == "borderless", "windowmode completes its values" );
		C.Check( Registry.Complete( "help fp" ).Candidates.Num() == 1 && Registry.Complete( "help fp" ).Candidates[0] == "fps", "help completes command names" );
		C.Check( Registry.Complete( "log LogAssetSelf" ).Candidates.Num() >= 1 && Registry.Complete( "log LogAssetSelfTest " ).Candidates.Num() == 8, "log completes the category names and then the levels" );
		const FConsoleCompletion Sets = Registry.Complete( "set " );
		bool bOnlyVariables = !Sets.Candidates.IsEmpty();
		for ( const FString& Candidate : Sets.Candidates ) bOnlyVariables = bOnlyVariables && Registry.Find( Candidate ) != nullptr && Registry.Find( Candidate )->bVariable;
		C.Check( bOnlyVariables, "set completes variable names only" );
	}
}

void NAssetTest::RunConsoleLogicTests( FChecker& C )
{
	TestParse( C );
	TestArgs( C );
	TestBufferHistoryTime( C );
	TestRegistry( C );
	TestCompletion( C );
	TestStaticRegistration( C );
	TestBuiltins( C );
}

//=============================================================================
// 動いているシーンに対するコマンド（オブジェクト・ECS・シーン）
//=============================================================================
void NAssetTest::RunConsoleSceneChecks( FChecker& C, CScene& Scene )
{
	C.Group = "console-scene";
	CEngine* Engine = CEngine::Get();
	CGameInstance* GameInstance = Engine != nullptr ? Engine->GetGameInstance() : nullptr;
	if ( GameInstance == nullptr || GameInstance->GetSceneManager().GetCurrentScene() != &Scene )
	{
		C.Skip( "the test scene is not the current scene" );
		return;
	}

	FConsoleRegistry Registry;
	NConsole::RegisterBuiltinCommands( Registry );

	FRun R = Run( Registry, "scene" );
	C.Check( R.Result == EConsoleResult::Ok && R.Has( "current scene: AssetSelfTest" ), "scene shows the current scene (%s)", *R.All );
	R = Run( Registry, "scenes" );
	C.Check( R.Has( "registered:" ) && R.Has( "AssetSelfTest" ) && R.Has( "stack (bottom to top)" ), "scenes lists the registered scenes and the stack" );
	C.Check( Run( Registry, "scene nosuchscene" ).Result == EConsoleResult::Failed && Run( Registry, "push nosuchscene" ).Result == EConsoleResult::Failed, "an unknown scene is refused" );
	R = Run( Registry, "ecs" );
	C.Check( R.Result == EConsoleResult::Ok && R.Has( "entit" ) && R.Has( "system(s)" ), "ecs shows the statistics" );
	R = Run( Registry, "systems" );
	C.Check( R.Result == EConsoleResult::Ok && R.Has( "system(s)" ) && R.Has( "PreUpdate" ), "systems lists the systems with their phase" );
	C.Check( Registry.Find( "scene" )->Args[0].Completer != nullptr && Registry.Complete( "scene Asset" ).Candidates.Num() == 1, "scene completes the registered scene names" );

	TSharedPtr<CGameObject> Object = Scene.SpawnEntityAt( FVector3( 1.0f, 2.0f, 3.0f ) );
	Object->SetName( "ConsoleTestObject" );
	const FString Id = FString::FromInt( static_cast<int64>( Object->GetId() ) );
	R = Run( Registry, "objects ConsoleTest" );
	C.Check( R.Has( "#" + Id ) && R.Has( "ConsoleTestObject" ) && R.Has( "(1.000, 2.000, 3.000)" ) && R.Has( "1 object(s) matched" ), "objects lists the object with its id and position (%s)", *R.All );
	R = Run( Registry, "object " + Id );
	C.Check( R.Result == EConsoleResult::Ok && R.Has( "ConsoleTestObject" ) && R.Has( "world position (1.000, 2.000, 3.000)" ) && R.Has( "component:" ) && R.Has( "visible yes, tick on" ), "object shows the details" );
	C.Check( Run( Registry, "object 99999999" ).Result == EConsoleResult::Failed && Run( Registry, "object -1" ).Result == EConsoleResult::Failed && Run( Registry, "object abc" ).Result == EConsoleResult::BadArguments, "an unknown id is refused" );

	Run( Registry, "teleport " + Id + " 4 5 6" );
	const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>();
	C.Check( Transform != nullptr && Transform->GetWorldPosition().x == 4.0f && Transform->GetWorldPosition().y == 5.0f && Transform->GetWorldPosition().z == 6.0f, "teleport moves the object" );
	C.Check( Run( Registry, "teleport " + Id + " 4 5" ).Result == EConsoleResult::BadArguments, "teleport needs three numbers" );
	Run( Registry, "visible " + Id + " off" );
	C.Check( !Object->IsVisible(), "visible off hides the object" );
	Run( Registry, "visible " + Id );
	C.Check( Object->IsVisible(), "visible without a value switches" );
	Run( Registry, "tick " + Id + " false" );
	C.Check( !Object->IsTickEnabled(), "tick off stops the object's Tick" );
	R = Run( Registry, "objects ConsoleTest" );
	C.Check( R.Has( "[no tick]" ), "objects marks it" );
	Run( Registry, "tick " + Id + " on" );
	C.Check( Object->IsTickEnabled(), "tick on" );
	R = Run( Registry, "destroy " + Id );
	C.Check( R.Result == EConsoleResult::Ok && Object->IsPendingDestroy(), "destroy schedules the destruction" );
	Object.Reset();
}
