#include "LogManager.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Library/Core/Thread/Mutex.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
	// "C:\\foo\\bar\\Hoge.cpp" -> "Hoge.cpp"
	const char* ExtractFileName( const char* Path )
	{
		if ( !Path ) return "";
		const char* Result = Path;
		for ( const char* p = Path; *p; ++p )
		{
			if ( *p == '\\' || *p == '/' ) Result = p + 1;
		}
		return Result;
	}

	// "Namespace::SomeFeature::SomeFunc" -> "Namespace: SomeFeature: SomeFunc"
	FString FormatFunctionName( const char* Function )
	{
		return FString( Function ).Replace( "::", ": " );
	}

	bool ShouldPrintVerbosity( ELogVerbosity Verbosity )
	{
		return Verbosity == ELogVerbosity::Fatal
			|| Verbosity == ELogVerbosity::Error
			|| Verbosity == ELogVerbosity::Warning;
	}
}

DEFINE_LOG_CATEGORY( LogTemp )

#pragma region FLogCategory

FLogCategory::FLogCategory( const char* InName, ELogVerbosity InDefaultVerbosity )
	: Name( InName )
	, DefaultVerbosity( InDefaultVerbosity )
	, Verbosity( InDefaultVerbosity )
{
	CLogManager::Get().RegisterCategory( this );
}

FLogCategory::~FLogCategory()
{
	CLogManager::Get().UnregisterCategory( this );
}

#pragma endregion

#pragma region CLogManager

CLogManager& CLogManager::Get()
{
	static CLogManager Instance;
	return Instance;
}

void CLogManager::AddSink( const TSharedPtr<ILogSink>& Sink )
{
	if ( !Sink ) return;

	FScopedLock Lock( SinkMutex );
	Sinks.AddUnique( Sink );
}

void CLogManager::RemoveSink( const TSharedPtr<ILogSink>& Sink )
{
	FScopedLock Lock( SinkMutex );
	Sinks.Remove( Sink );
}

void CLogManager::RemoveAllSinks()
{
	FScopedLock Lock( SinkMutex );
	for ( auto& Sink : Sinks )
	{
		Sink->Flush();
	}
	Sinks.Reset();
}

void CLogManager::Flush()
{
	FScopedLock Lock( SinkMutex );
	for ( auto& Sink : Sinks )
	{
		Sink->Flush();
	}
}

void CLogManager::SetAllCategoriesVerbosity( ELogVerbosity Verbosity )
{
	FScopedLock Lock( CategoryMutex );
	for ( FLogCategory* Category : Categories ) Category->SetVerbosity( Verbosity );
}

void CLogManager::ResetAllCategoriesToDefault()
{
	FScopedLock Lock( CategoryMutex );
	for ( FLogCategory* Category : Categories ) Category->SetVerbosity( Category->GetDefaultVerbosity() );
}

int32 CLogManager::ApplyCommandLine( const char* CommandLine )
{
	if ( CommandLine == nullptr ) return 0;

	int32 NumChanged = 0;
	for ( const FString& Token : FString( CommandLine ).ParseIntoArray( " " ) )
	{
		if ( Token == "--log-verbose" || Token == "--log-veryverbose" )
		{
			const ELogVerbosity Verbosity = Token == "--log-verbose" ? ELogVerbosity::Verbose : ELogVerbosity::VeryVerbose;
			SetAllCategoriesVerbosity( Verbosity );
			NumChanged += GetCategories().Num();
		}
		else if ( Token.StartsWith( "--log=" ) )
		{
			// --log=Cat:Level,Cat:Level
			for ( const FString& Pair : Token.RightChop( 6 ).ParseIntoArray( "," ) )
			{
				FString CategoryName, LevelName;
				ELogVerbosity Verbosity = ELogVerbosity::Log;
				if ( !Pair.Split( ":", &CategoryName, &LevelName ) || !LogVerbosityFromString( *LevelName, Verbosity ) )
				{
					LOG( LogTemp, Warning, "Ignored '%s': expected Category:Level (NoLogging / Fatal / Error / Warning / Display / Log / Verbose / VeryVerbose)", *Pair );
					continue;
				}
				if ( !SetCategoryVerbosity( *CategoryName, Verbosity ) )
				{
					LOG( LogTemp, Warning, "Ignored '%s': no log category named '%s'", *Pair, *CategoryName );
					continue;
				}
				++NumChanged;
			}
		}
	}
	return NumChanged;
}

FLogCategory* CLogManager::FindCategory( const char* Name )
{
	if ( !Name ) return nullptr;

	FScopedLock Lock( CategoryMutex );
	FLogCategory* const* Found = Categories.FindByPredicate( [Name]( const FLogCategory* Category )
	{
		return std::strcmp( Category->GetName(), Name ) == 0;
	} );
	return Found ? *Found : nullptr;
}

bool CLogManager::SetCategoryVerbosity( const char* Name, ELogVerbosity Verbosity )
{
	FLogCategory* Category = FindCategory( Name );
	if ( !Category ) return false;

	Category->SetVerbosity( Verbosity );
	return true;
}

TArray<FLogCategory*> CLogManager::GetCategories()
{
	FScopedLock Lock( CategoryMutex );
	return Categories;
}

void CLogManager::Log( const FLogCategory& Category, ELogVerbosity Verbosity,
					   const char* File, int32 Line, const char* Function,
					   const char* Format, ... )
{
	va_list Args;
	va_start( Args, Format );
	LogV( Category, Verbosity, File, Line, Function, Format, Args );
	va_end( Args );
}

void CLogManager::LogV( const FLogCategory& Category, ELogVerbosity Verbosity,
						const char* File, int32 Line, const char* Function,
						const char* Format, va_list Args )
{
	if ( Verbosity == ELogVerbosity::NoLogging ) return;
	if ( Category.IsSuppressed( Verbosity ) ) return;

	FLogMessage Message;
	Message.CategoryName = Category.GetName();
	Message.Verbosity    = Verbosity;
	Message.Message      = FString::PrintfV( Format, Args );
	Message.Function     = FormatFunctionName( Function );
	Message.FileName     = ExtractFileName( File );
	Message.Line         = Line;
	Message.Time         = std::chrono::system_clock::now();

	// [LogHoge]: (Warning: )SomeFeature: SomeFunc: Anything message. | Hoge.cpp | line: 42
	FString& Out = Message.FormattedLine;
	Out.Reserve( Message.Message.Len() + Message.Function.Len() + 64 );
	Out += '[';
	Out += Message.CategoryName;
	Out += "]: ";
	if ( ShouldPrintVerbosity( Verbosity ) )
	{
		Out += LogVerbosityToString( Verbosity );
		Out += ": ";
	}
	if ( !Message.Function.IsEmpty() )
	{
		Out += Message.Function;
		Out += ": ";
	}
	Out += Message.Message;
	Out += " | ";
	Out += Message.FileName;
	Out += " | line: ";
	Out += FString::FromInt( Line );

	{
		FScopedLock Lock( SinkMutex );
		for ( auto& Sink : Sinks )
		{
			if ( Verbosity <= Sink->GetMaxVerbosity() )
			{
				Sink->Write( Message );
			}
		}

		if ( Verbosity <= ELogVerbosity::Error )
		{
			for ( auto& Sink : Sinks )
			{
				Sink->Flush();
			}
		}
	}

	if ( Verbosity == ELogVerbosity::Fatal )
	{
		if ( IsDebuggerPresent() )
		{
			__debugbreak();
		}
		std::abort();
	}
}

void CLogManager::RegisterCategory( FLogCategory* Category )
{
	FScopedLock Lock( CategoryMutex );
	Categories.Add( Category );
}

void CLogManager::UnregisterCategory( FLogCategory* Category )
{
	FScopedLock Lock( CategoryMutex );
	Categories.Remove( Category );
}

#pragma endregion
