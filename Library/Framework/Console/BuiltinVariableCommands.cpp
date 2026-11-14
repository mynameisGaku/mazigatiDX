// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: 変数（一覧・読む・書く）
void NConsole::NBuiltin::RegisterVariableCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "vars", "List the console variables and their values",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 int32 Count = 0;
			 for ( const FConsoleCommandInfo* Info : Context.GetRegistry().GetCommands() )
			 {
				 if ( !Info->bVariable ) continue;
				 if ( Filter.IsSet() && !Info->Name.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 FString Value;
				 Context.GetRegistry().GetVariableText( Info->Name, Value );
				 Context.Print( "  " + Pad( Info->Name, 22 ) + Pad( Value, 14 ) + Info->Description );
				 ++Count;
			 }
			 if ( Count == 0 ) Context.Print( "(no variables)" );
		 },
		 { { "filter", "Only the variables whose name contains this text." } }, CategoryVariables );

	Add( "set", "Set a console variable (same as typing its name and the value)",
		 []( FConsoleContext& Context, const FString& Name, const FString& Value )
		 {
			 const FConsoleCommandInfo* Info = Context.GetRegistry().Find( Name );
			 if ( Info == nullptr || !Info->bVariable )
			 {
				 Context.Error( "'" + Name + "' is not a variable (see 'vars')" );
				 return false;
			 }
			 return Context.GetRegistry().Execute( NConsole::Quote( Name ) + " " + NConsole::Quote( Value ), Context.GetOutput() ) == EConsoleResult::Ok;
		 },
		 { { "name", "The variable name." }, { "value", "The new value." } }, CategoryVariables );
	Registry.SetArgCompleter( "set", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			if ( Info->bVariable ) Names.Add( Info->Name );
		}
		return Names;
	} );

	Add( "toggle", "Flip a true / false console variable",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 FString Text;
			 bool bValue = false;
			 if ( !Context.GetRegistry().GetVariableText( Name, Text ) || !NConsole::ParseBool( Text, bValue ) )
			 {
				 Context.Error( "'" + Name + "' is not a true / false variable (see 'vars')" );
				 return false;
			 }
			 return Context.GetRegistry().Execute( NConsole::Quote( Name ) + ( bValue ? " false" : " true" ), Context.GetOutput() ) == EConsoleResult::Ok;
		 },
		 { { "name", "The variable name." } }, CategoryVariables );
	Registry.SetArgCompleter( "toggle", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			FString Text;
			bool bValue = false;
			if ( Info->bVariable && Registry.GetVariableText( Info->Name, Text ) && NConsole::ParseBool( Text, bValue ) ) Names.Add( Info->Name );
		}
		return Names;
	} );

	Add( "reset", "Put a console variable back to the value it had when it was registered",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 if ( !Context.GetRegistry().ResetVariable( Name ) )
			 {
				 Context.Error( "'" + Name + "' is not a variable (see 'vars')" );
				 return false;
			 }
			 FString Value;
			 Context.GetRegistry().GetVariableText( Name, Value );
			 Context.Print( Name + " = " + Value );
			 return true;
		 },
		 { { "name", "The variable name." } }, CategoryVariables );
	Registry.SetArgCompleter( "reset", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			if ( Info->bVariable ) Names.Add( Info->Name );
		}
		return Names;
	} );

	// 変数
	Registry.AddVariable<float32>( "timescale", []() { CEngine* Engine = CEngine::Get(); return Engine != nullptr ? Engine->GetTimeControl().GetScale() : 1.0f; },
								   []( const float32& Value ) { if ( CEngine* Engine = CEngine::Get() ) Engine->GetTimeControl().SetScale( Value ); },
								   "Game speed multiplier (1 = normal, 0.5 = half speed, 0 = frozen)",
								   [] { TConsoleVariableOptions<float32> Options; Options.Min = 0.0f; Options.Max = FEngineTimeControl::MaxScale; return Options; }(), CategoryEngine ).Keep();
	Registry.AddVariable<FString>( "window.title", []() { IPlatform* Platform = GetAppSubsystem<IPlatform>(); return Platform != nullptr ? Platform->GetWindowTitle() : FString(); },
								   []( const FString& Value ) { if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->SetWindowTitle( Value ); },
								   "The window title", TConsoleVariableOptions<FString>(), CategoryWindow ).Keep();
	Registry.AddVariable<bool>( "cursor.visible", []() { IPlatform* Platform = GetAppSubsystem<IPlatform>(); return Platform != nullptr && Platform->IsMouseCursorVisible(); },
								[]( const bool& Value ) { if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->SetMouseCursorVisible( Value ); },
								"Whether the mouse cursor is shown", TConsoleVariableOptions<bool>(), CategoryWindow ).Keep();
	Registry.AddVariable<float32>( "audio.master", []() { CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>(); return Audio != nullptr ? Audio->GetBusVolume( EAudioBus::Master ) : 1.0f; },
								   []( const float32& Value ) { if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() ) Audio->SetBusVolume( EAudioBus::Master, Value ); },
								   "Master volume (1 = normal)", [] { TConsoleVariableOptions<float32> Options; Options.Min = 0.0f; Options.Max = 2.0f; return Options; }(), CategoryAudio ).Keep();
}
