// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: ログ
void NConsole::NBuiltin::RegisterLogCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "log", "Show or change the log level of the log categories",
		 []( FConsoleContext& Context, TOptional<FString> Category, TOptional<FString> Level )
		 {
			 CLogManager& Manager = CLogManager::Get();
			 if ( !Category.IsSet() )
			 {
				 TArray<FLogCategory*> Categories = Manager.GetCategories();
				 Categories.StableSort( []( const FLogCategory* A, const FLogCategory* B ) { return FString( A->GetName() ) < FString( B->GetName() ); } );
				 for ( const FLogCategory* Item : Categories )
				 {
					 Context.Print( FString::Printf( "  %-24s %-12s%s", Item->GetName(), LogVerbosityToString( Item->GetVerbosity() ), Item->GetVerbosity() != Item->GetDefaultVerbosity() ? *FString::Printf( "(default %s)", LogVerbosityToString( Item->GetDefaultVerbosity() ) ) : "" ) );
				 }
				 return true;
			 }
			 if ( Category.GetValue().Equals( "reset", ESearchCase::IgnoreCase ) )
			 {
				 Manager.ResetAllCategoriesToDefault();
				 Context.Print( "all log levels are back to their defaults" );
				 return true;
			 }
			 ELogVerbosity Verbosity = ELogVerbosity::Log;
			 if ( Level.IsSet() && !LogVerbosityFromString( *Level.GetValue(), Verbosity ) )
			 {
				 Context.Error( "unknown level '" + Level.GetValue() + "' (" + FString::Join( GetVerbosityNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Category.GetValue().Equals( "all", ESearchCase::IgnoreCase ) )
			 {
				 if ( !Level.IsSet() )
				 {
					 Context.Error( "give a level: log all <level>" );
					 return false;
				 }
				 Manager.SetAllCategoriesVerbosity( Verbosity );
				 Context.Print( FString( "all log categories: " ) + LogVerbosityToString( Verbosity ) );
				 return true;
			 }
			 FLogCategory* Found = FindLogCategory( Category.GetValue() );
			 if ( Found == nullptr )
			 {
				 Context.Error( "no log category named '" + Category.GetValue() + "' (see 'log')" );
				 return false;
			 }
			 if ( Level.IsSet() ) Found->SetVerbosity( Verbosity );
			 Context.Print( FString( Found->GetName() ) + ": " + LogVerbosityToString( Found->GetVerbosity() ) );
			 return true;
		 },
		 { { "category", "A log category name (for example LogAsset), 'all', or 'reset' to restore every default. Without it, lists the categories." },
		   { "level", "NoLogging, Fatal, Error, Warning, Display, Log, Verbose or VeryVerbose. Without it, shows the level." } },
		 CategoryLog );
	Registry.SetArgCompleter( "log", 0, []()
	{
		TArray<FString> Names = GetLogCategoryNames();
		Names.Add( "all" );
		Names.Add( "reset" );
		return Names;
	} );
	Registry.SetArgChoices( "log", 1, GetVerbosityNames() );
}
