// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: デバッグビュー（ゲームの上に重ねて描く表示）
void NConsole::NBuiltin::RegisterDebugViewCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "view", "List the debug views (overlays such as the physics shapes), or turn one on / off / toggle it",
		 []( FConsoleContext& Context, TOptional<FString> Name, TOptional<FString> State )
		 {
			 const FDebugViewRegistry& Views = FDebugViewRegistry::Global();
			 if ( !Name.IsSet() )
			 {
				 if ( Views.Num() == 0 )
				 {
					 Context.Print( "no debug views yet (a system registers its view when it starts, e.g. Physics in a scene with physics)" );
					 return true;
				 }
				 for ( int32 Index = 0; Index < Views.Num(); ++Index )
				 {
					 const FDebugView& View = Views.Get( Index );
					 Context.Print( View.Name + ": " + ( *View.pEnabled ? "on" : "off" ) + ( View.bHasHotkey ? "  [" + FDebugViewRegistry::GetKeyLabel( View.Hotkey ) + "]" : FString() ) + "  " + View.Description );
				 }
				 return true;
			 }
			 const FDebugView* View = Views.Find( Name.GetValue() );
			 if ( View == nullptr )
			 {
				 Context.Error( "no debug view named '" + Name.GetValue() + "' (use 'view' to list them)" );
				 return false;
			 }
			 if ( State.IsSet() )
			 {
				 const FString& Text = State.GetValue();
				 if ( Text.Equals( "toggle", ESearchCase::IgnoreCase ) ) Views.Toggle( View->Name );
				 else if ( Text.Equals( "on", ESearchCase::IgnoreCase ) || Text.Equals( "true", ESearchCase::IgnoreCase ) || Text == "1" ) Views.SetEnabled( View->Name, true );
				 else if ( Text.Equals( "off", ESearchCase::IgnoreCase ) || Text.Equals( "false", ESearchCase::IgnoreCase ) || Text == "0" ) Views.SetEnabled( View->Name, false );
				 else
				 {
					 Context.Error( "'" + Text + "' is not on / off / toggle" );
					 return false;
				 }
			 }
			 Context.Print( View->Name + ": " + ( *View->pEnabled ? "on" : "off" ) );
			 return true;
		 },
		 { { "name", "Name of the view. Without it, all views are listed." }, { "state", "on / off / toggle. Without it, shows the state." } }, CategoryViews );
	Registry.SetArgCompleter( "view", 0, []()
	{
		TArray<FString> Names;
		for ( int32 Index = 0; Index < FDebugViewRegistry::Global().Num(); ++Index ) Names.Add( FDebugViewRegistry::Global().Get( Index ).Name );
		return Names;
	} );
	Registry.SetArgChoices( "view", 1, { "on", "off", "toggle" } );

	Add( "view.set", "Set an option of a debug view (view.set Physics radius 30). Without the option, lists the options; without the value, shows it",
		 []( FConsoleContext& Context, const FString& Name, TOptional<FString> Option, TOptional<FString> Value )
		 {
			 const FDebugViewRegistry& Views = FDebugViewRegistry::Global();
			 const FDebugView* View = Views.Find( Name );
			 if ( View == nullptr )
			 {
				 Context.Error( "no debug view named '" + Name + "' (use 'view' to list them)" );
				 return false;
			 }
			 if ( !Option.IsSet() )
			 {
				 for ( const FDebugViewOption& Item : View->Options )
				 {
					 Context.Print( FString( Item.Name ) + " = " + FDebugViewRegistry::GetOptionText( Item ) + "  " + Item.Description );
				 }
				 return true;
			 }
			 if ( !Value.IsSet() )
			 {
				 const FDebugViewOption* Item = FDebugViewRegistry::FindOption( *View, Option.GetValue() );
				 if ( Item == nullptr )
				 {
					 Context.Error( "the view '" + View->Name + "' has no option '" + Option.GetValue() + "'" );
					 return false;
				 }
				 Context.Print( FString( Item->Name ) + " = " + FDebugViewRegistry::GetOptionText( *Item ) );
				 return true;
			 }
			 FString Error;
			 if ( !Views.SetOption( Name, Option.GetValue(), Value.GetValue(), &Error ) )
			 {
				 Context.Error( Error );
				 return false;
			 }
			 const FDebugViewOption* Item = FDebugViewRegistry::FindOption( *View, Option.GetValue() );
			 Context.Print( FString( Item->Name ) + " = " + FDebugViewRegistry::GetOptionText( *Item ) );
			 return true;
		 },
		 { { "name", "Name of the view." }, { "option", "Name of the option. Without it, lists them." }, { "value", "New value (on / off or a number). Without it, shows the value." } }, CategoryViews );
	Registry.SetArgCompleter( "view.set", 0, []()
	{
		TArray<FString> Names;
		for ( int32 Index = 0; Index < FDebugViewRegistry::Global().Num(); ++Index ) Names.Add( FDebugViewRegistry::Global().Get( Index ).Name );
		return Names;
	} );
}
