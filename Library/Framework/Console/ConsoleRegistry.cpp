#include "ConsoleRegistry.h"

#include <algorithm>

#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Console/ConsoleParse.h"

namespace
{
	constexpr int32 MaxAliasDepth = 8;

	FString GetGroupName( const FConsoleCommandInfo& Info )
	{
		return Info.Category.IsEmpty() ? FString( "General" ) : Info.Category;
	}

	// 編集距離（小文字の ASCII 前提の、簡単なもの）
	int32 EditDistance( const FString& A, const FString& B )
	{
		const int32 N = A.Len(), M = B.Len();
		if ( N == 0 ) return M;
		if ( M == 0 ) return N;
		TArray<int32> Previous, Current;
		Previous.SetNum( M + 1 );
		Current.SetNum( M + 1 );
		for ( int32 j = 0; j <= M; ++j ) Previous[j] = j;
		for ( int32 i = 1; i <= N; ++i )
		{
			Current[0] = i;
			for ( int32 j = 1; j <= M; ++j )
			{
				const int32 Cost = A[i - 1] == B[j - 1] ? 0 : 1;
				Current[j] = NMath::Min( NMath::Min( Previous[j] + 1, Current[j - 1] + 1 ), Previous[j - 1] + Cost );
			}
			std::swap( Previous, Current );
		}
		return Previous[M];
	}

	bool StartsWithIgnoreCase( const FString& Text, const FString& Prefix )
	{
		return Text.StartsWith( Prefix, ESearchCase::IgnoreCase );
	}

	// 全部の先頭が同じ部分（大文字小文字は区別しない。2 バイト文字の途中では切らない）
	FString CommonPrefixOf( const TArray<FString>& Texts )
	{
		if ( Texts.IsEmpty() ) return FString();
		FString Prefix = Texts[0];
		for ( int32 n = 1; n < Texts.Num(); ++n )
		{
			int32 Length = 0;
			const int32 Max = NMath::Min( Prefix.Len(), Texts[n].Len() );
			while ( Length < Max && FString( &Prefix[Length], 1 ).Equals( FString( &Texts[n][Length], 1 ), ESearchCase::IgnoreCase ) ) ++Length;
			Prefix = Prefix.Left( Length );
		}
		// 2 バイト文字の 1 バイト目で終わっていたら、その 1 バイトは落とす
		int32 Index = 0;
		while ( Index < Prefix.Len() )
		{
			if ( NConsole::IsLeadByte( static_cast<unsigned char>( Prefix[Index] ) ) )
			{
				if ( Index + 1 >= Prefix.Len() ) return Prefix.Left( Index );
				Index += 2;
			}
			else ++Index;
		}
		return Prefix;
	}
}

//=============================================================================
// FConsoleCommandInfo
//=============================================================================
FString FConsoleCommandInfo::GetUsage() const
{
	FString Usage = Name;
	for ( const FConsoleArgDesc& Arg : Args )
	{
		const FString Body = Arg.Name + ":" + Arg.TypeName;
		Usage += Arg.bOptional ? " [" + Body + "]" : " <" + Body + ">";
	}
	return Usage;
}

//=============================================================================
// FConsoleRegistration
//=============================================================================
void FConsoleRegistration::Unregister()
{
	if ( Registry != nullptr )
	{
		Registry->UnregisterById( Id );
		Registry = nullptr;
	}
}

//=============================================================================
// FConsoleRegistry
//=============================================================================
FConsoleRegistry& FConsoleRegistry::Global()
{
	static FConsoleRegistry Instance;
	return Instance;
}

FConsoleRegistration FConsoleRegistry::Register( FEntry&& Entry )
{
	Entry.Id = NextId++;
	FConsoleRegistration Registration;
	Registration.Registry = this;
	Registration.Id = Entry.Id;
	const FString Key = MakeKey( Entry.Info.Name );
	// コマンドと同じ名前の別名は、コマンドが優先されるので外す
	Aliases.Remove( Key );
	Entries.Add( Key, MoveTemp( Entry ) );
	return Registration;
}

FConsoleRegistration FConsoleRegistry::AddRaw( const FString& Name, const FString& Description, TCallable<EConsoleResult( FConsoleContext& )> Function, const TArray<FConsoleArgDesc>& Args, const FString& Category )
{
	FEntry Entry;
	Entry.Info.Name = Name;
	Entry.Info.Description = Description;
	Entry.Info.Category = Category;
	Entry.Info.Args = Args;
	Entry.Handler = std::make_shared<FHandler>( [Function]( FConsoleContext& Context, const TArray<FString>&, FString& ) { return Function( Context ); } );
	return Register( MoveTemp( Entry ) );
}

void FConsoleRegistry::MarkVariable( const FString& Name, TCallable<FString()> GetText, TCallable<void()> Reset )
{
	if ( FEntry* Entry = Entries.Find( MakeKey( Name ) ) )
	{
		Entry->Info.bVariable = true;
		Entry->GetText = MoveTemp( GetText );
		Entry->Reset = MoveTemp( Reset );
	}
}

void FConsoleRegistry::UnregisterById( uint32 Id )
{
	for ( auto It = Entries.CreateIterator(); It; ++It )
	{
		if ( It.Value().Id == Id )
		{
			It.RemoveCurrent();
			return;
		}
	}
}

bool FConsoleRegistry::Remove( const FString& Name )
{
	return Entries.Remove( MakeKey( Name ) ) > 0;
}

//-----------------------------------------------------------------------------
// 別名
//-----------------------------------------------------------------------------
bool FConsoleRegistry::SetAlias( const FString& Alias, const FString& Line, FString& OutError )
{
	const FString Name = Alias.TrimStartAndEnd();
	if ( Name.IsEmpty() )
	{
		OutError = "the alias needs a name";
		return false;
	}
	for ( int32 i = 0; i < Name.Len(); ++i )
	{
		const char Ch = Name[i];
		if ( Ch == ' ' || Ch == '\t' || Ch == ';' || Ch == '"' || Ch == '\'' )
		{
			OutError = "an alias name cannot contain spaces, quotes or ';'";
			return false;
		}
	}
	if ( Line.TrimStartAndEnd().IsEmpty() )
	{
		OutError = "the alias needs a command line";
		return false;
	}
	if ( Entries.Find( MakeKey( Name ) ) != nullptr )
	{
		OutError = "'" + Name + "' is already a command";
		return false;
	}
	Aliases.Add( MakeKey( Name ), TPair<FString, FString> { Name, Line.TrimStartAndEnd() } );
	return true;
}

bool FConsoleRegistry::RemoveAlias( const FString& Alias )
{
	return Aliases.Remove( MakeKey( Alias ) ) > 0;
}

const FString* FConsoleRegistry::FindAlias( const FString& Alias ) const
{
	const TPair<FString, FString>* Found = Aliases.Find( MakeKey( Alias ) );
	return Found != nullptr ? &Found->Value : nullptr;
}

TArray<TPair<FString, FString>> FConsoleRegistry::GetAliases() const
{
	TArray<TPair<FString, FString>> Result;
	for ( const auto& Pair : Aliases ) Result.Add( Pair.Value );
	std::sort( Result.begin(), Result.end(), []( const TPair<FString, FString>& A, const TPair<FString, FString>& B ) { return A.Key.ToLower() < B.Key.ToLower(); } );
	return Result;
}

//-----------------------------------------------------------------------------
// 実行
//-----------------------------------------------------------------------------
EConsoleResult FConsoleRegistry::Execute( const FString& Line, IConsoleOutput& Output )
{
	const TArray<FString> Segments = NConsole::SplitCommands( Line );
	if ( Segments.IsEmpty() ) return EConsoleResult::Empty;

	EConsoleResult First = EConsoleResult::Ok;
	for ( const FString& Segment : Segments )
	{
		const EConsoleResult Result = ExecuteSegment( Segment, Output, 0 );
		if ( Result != EConsoleResult::Ok && First == EConsoleResult::Ok ) First = Result;
	}
	return First;
}

FString FConsoleRegistry::SuggestNames( const FString& Name ) const
{
	const FString Lower = Name.ToLower();
	TArray<FString> Matches;
	const auto Consider = [&]( const FString& Candidate )
	{
		const FString CandidateLower = Candidate.ToLower();
		if ( CandidateLower.StartsWith( Lower ) || EditDistance( Lower, CandidateLower ) <= 2 ) Matches.AddUnique( Candidate );
	};
	for ( const auto& Pair : Entries )
	{
		if ( !Pair.Value.Info.bHidden ) Consider( Pair.Value.Info.Name );
	}
	for ( const auto& Pair : Aliases ) Consider( Pair.Value.Key );
	if ( Matches.IsEmpty() ) return FString();

	std::sort( Matches.begin(), Matches.end(), [&]( const FString& A, const FString& B )
	{
		const int32 DistanceA = EditDistance( Lower, A.ToLower() ), DistanceB = EditDistance( Lower, B.ToLower() );
		return DistanceA != DistanceB ? DistanceA < DistanceB : A < B;
	} );
	if ( Matches.Num() > 4 ) Matches.SetNum( 4 );
	return FString::Join( Matches, ", " );
}

EConsoleResult FConsoleRegistry::ExecuteSegment( const FString& Segment, IConsoleOutput& Output, int32 Depth )
{
	TArray<FString> Tokens;
	FString Error;
	if ( !NConsole::Tokenize( Segment, Tokens, Error ) )
	{
		Output.Write( EConsoleLine::Error, Error );
		return EConsoleResult::BadArguments;
	}
	if ( Tokens.IsEmpty() ) return EConsoleResult::Empty;

	const FString Name = Tokens[0];
	Tokens.RemoveAt( 0 );
	const FString Key = MakeKey( Name );

	if ( FEntry* Entry = Entries.Find( Key ) )
	{
		const std::shared_ptr<FHandler> Handler = Entry->Handler;   // 実行中にコマンドが増減しても、この関数は消えない
		const FConsoleCommandInfo Info = Entry->Info;               // 同上（使い方の表示用）
		FConsoleContext Context( *this, Output, Info.Name, Tokens );
		FString ArgError;
		const EConsoleResult Result = ( *Handler )( Context, Tokens, ArgError );
		if ( Result == EConsoleResult::BadArguments )
		{
			Output.Write( EConsoleLine::Error, ArgError );
			Output.Write( EConsoleLine::Info, "usage: " + Info.GetUsage() );
		}
		return Result;
	}

	if ( const TPair<FString, FString>* Alias = Aliases.Find( Key ) )
	{
		if ( Depth >= MaxAliasDepth )
		{
			Output.Write( EConsoleLine::Error, "the alias '" + Name + "' calls itself too deeply (more than " + FString::FromInt( MaxAliasDepth ) + " levels)" );
			return EConsoleResult::Failed;
		}
		FString Expanded = Alias->Value;
		for ( const FString& Token : Tokens ) Expanded += " " + NConsole::Quote( Token );
		EConsoleResult First = EConsoleResult::Ok;
		for ( const FString& Sub : NConsole::SplitCommands( Expanded ) )
		{
			const EConsoleResult Result = ExecuteSegment( Sub, Output, Depth + 1 );
			if ( Result != EConsoleResult::Ok && First == EConsoleResult::Ok ) First = Result;
		}
		return First;
	}

	FString Message = "unknown command '" + Name + "'";
	const FString Suggestions = SuggestNames( Name );
	if ( !Suggestions.IsEmpty() ) Message += ". Did you mean: " + Suggestions + "?";
	else Message += ". Type 'help' for the list of commands.";
	Output.Write( EConsoleLine::Error, Message );
	return EConsoleResult::UnknownCommand;
}

//-----------------------------------------------------------------------------
// 問い合わせ
//-----------------------------------------------------------------------------
const FConsoleCommandInfo* FConsoleRegistry::Find( const FString& Name ) const
{
	const FEntry* Entry = Entries.Find( MakeKey( Name ) );
	return Entry != nullptr ? &Entry->Info : nullptr;
}

TArray<const FConsoleCommandInfo*> FConsoleRegistry::GetCommands( bool bIncludeHidden ) const
{
	TArray<const FConsoleCommandInfo*> Result;
	for ( const auto& Pair : Entries )
	{
		if ( bIncludeHidden || !Pair.Value.Info.bHidden ) Result.Add( &Pair.Value.Info );
	}
	std::sort( Result.begin(), Result.end(), []( const FConsoleCommandInfo* A, const FConsoleCommandInfo* B )
	{
		const FString GroupA = GetGroupName( *A ).ToLower(), GroupB = GetGroupName( *B ).ToLower();
		if ( GroupA != GroupB ) return GroupA < GroupB;
		return A->Name.ToLower() < B->Name.ToLower();
	} );
	return Result;
}

bool FConsoleRegistry::GetVariableText( const FString& Name, FString& OutText ) const
{
	const FEntry* Entry = Entries.Find( MakeKey( Name ) );
	if ( Entry == nullptr || !Entry->GetText ) return false;
	OutText = Entry->GetText();
	return true;
}

bool FConsoleRegistry::ResetVariable( const FString& Name )
{
	FEntry* Entry = Entries.Find( MakeKey( Name ) );
	if ( Entry == nullptr || !Entry->Reset ) return false;
	const TCallable<void()> Reset = Entry->Reset;
	Reset();
	return true;
}

bool FConsoleRegistry::SetArgChoices( const FString& Name, int32 ArgIndex, const TArray<FString>& Choices )
{
	FEntry* Entry = Entries.Find( MakeKey( Name ) );
	if ( Entry == nullptr || !Entry->Info.Args.IsValidIndex( ArgIndex ) ) return false;
	Entry->Info.Args[ArgIndex].Choices = Choices;
	return true;
}

bool FConsoleRegistry::SetArgCompleter( const FString& Name, int32 ArgIndex, TCallable<TArray<FString>()> Completer )
{
	FEntry* Entry = Entries.Find( MakeKey( Name ) );
	if ( Entry == nullptr || !Entry->Info.Args.IsValidIndex( ArgIndex ) ) return false;
	Entry->Info.Args[ArgIndex].Completer = MoveTemp( Completer );
	return true;
}

bool FConsoleRegistry::SetHidden( const FString& Name, bool bHidden )
{
	FEntry* Entry = Entries.Find( MakeKey( Name ) );
	if ( Entry == nullptr ) return false;
	Entry->Info.bHidden = bHidden;
	return true;
}

//-----------------------------------------------------------------------------
// 補完
//-----------------------------------------------------------------------------
FConsoleCompletion FConsoleRegistry::Complete( const FString& Line ) const
{
	const NConsole::FInputAnalysis Analysis = NConsole::AnalyzeInput( Line );
	FConsoleCompletion Completion;
	Completion.ReplaceFrom = Analysis.CurrentStart;

	TArray<TPair<FString, FString>> Raw;   // { 候補, 説明 }。引用符をつける前
	const auto AddRaw = [&Raw]( const FString& Text, const FString& Description )
	{
		for ( const TPair<FString, FString>& Existing : Raw )
		{
			if ( Existing.Key == Text ) return;
		}
		Raw.Add( TPair<FString, FString> { Text, Description } );
	};
	if ( Analysis.Tokens.IsEmpty() )
	{
		// コマンド名（別名も）
		for ( const auto& Pair : Entries )
		{
			if ( !Pair.Value.Info.bHidden && StartsWithIgnoreCase( Pair.Value.Info.Name, Analysis.CurrentPrefix ) ) AddRaw( Pair.Value.Info.Name, Pair.Value.Info.Description );
		}
		for ( const auto& Pair : Aliases )
		{
			if ( StartsWithIgnoreCase( Pair.Value.Key, Analysis.CurrentPrefix ) ) AddRaw( Pair.Value.Key, "alias: " + Pair.Value.Value );
		}
		// 名前を打ち終えている（完全に一致する）コマンドの使い方を見せる
		if ( const FConsoleCommandInfo* Typed = Find( Analysis.CurrentPrefix ) )
		{
			Completion.Usage = Typed->GetUsage();
			Completion.Command = Typed;
		}
	}
	else if ( const FConsoleCommandInfo* Info = Find( Analysis.Tokens[0] ) )
	{
		Completion.Usage = Info->GetUsage();
		Completion.Command = Info;
		// いま入力している引数（コマンド名を除いた、ここまでに入力し終えた引数の数）
		const int32 TokenIndex = Analysis.Tokens.Num() - 1;
		int32 Consumed = 0;
		int32 ArgIndex = INDEX_NONE;
		for ( int32 i = 0; i < Info->Args.Num(); ++i )
		{
			const int32 Width = Info->Args[i].NumTokens < 0 ? 0x7FFFFFFF : Info->Args[i].NumTokens;
			if ( TokenIndex < Consumed + Width || Info->Args[i].NumTokens < 0 )
			{
				ArgIndex = i;
				break;
			}
			Consumed += Width;
		}
		Completion.ActiveArg = ArgIndex;
		if ( ArgIndex != INDEX_NONE )
		{
			const FConsoleArgDesc& Arg = Info->Args[ArgIndex];
			TArray<FString> Source = Arg.Choices;
			if ( Arg.Completer ) Source.Append( Arg.Completer() );
			for ( const FString& Choice : Source )
			{
				if ( StartsWithIgnoreCase( Choice, Analysis.CurrentPrefix ) ) AddRaw( Choice, FString() );
			}
		}
	}

	std::sort( Raw.begin(), Raw.end(), []( const TPair<FString, FString>& A, const TPair<FString, FString>& B ) { return A.Key.ToLower() < B.Key.ToLower(); } );
	for ( const TPair<FString, FString>& Candidate : Raw )
	{
		Completion.Candidates.Add( NConsole::Quote( Candidate.Key ) );
		Completion.Descriptions.Add( Candidate.Value );
	}
	Completion.CommonPrefix = Completion.Candidates.Num() > 1 ? CommonPrefixOf( Completion.Candidates ) : ( Completion.Candidates.Num() == 1 ? Completion.Candidates[0] : FString() );
	return Completion;
}
