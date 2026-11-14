// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: AI（ステートマシン・ビヘイビアツリー）
void NConsole::NBuiltin::RegisterAICommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "ai.dump", "Show the state machines and behavior trees in the current scene (state, active nodes, blackboard, recent transitions)",
		 []( FConsoleContext& Context, TOptional<int64> ObjectId )
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr )
			 {
				 Context.Error( "no scene" );
				 return false;
			 }
			 int32 Count = 0;
			 for ( const TSharedPtr<CGameObject>& Object : Scene->GetObjects() )
			 {
				 CStateMachineComponent* Machine = Object.IsValid() ? Object->GetComponent<CStateMachineComponent>() : nullptr;
				 CBehaviorTreeComponent* Brain = Object.IsValid() ? Object->GetComponent<CBehaviorTreeComponent>() : nullptr;
				 if ( ( Machine == nullptr && Brain == nullptr ) || ( ObjectId.IsSet() && static_cast<int64>( Object->GetId() ) != ObjectId.GetValue() ) )
				 {
					 continue;
				 }
				 if ( Machine != nullptr )
				 {
					 Context.Print( FString::Printf( "#%llu %s: %s", static_cast<unsigned long long>( Object->GetId() ), *Object->GetDebugName(), *Machine->Describe() ) );
					 ++Count;
				 }
				 if ( Brain != nullptr )
				 {
					 Context.Print( FString::Printf( "#%llu %s: %s", static_cast<unsigned long long>( Object->GetId() ), *Object->GetDebugName(), *Brain->Describe() ) );
					 ++Count;
				 }
			 }
			 Context.Print( FString::Printf( "%d AI component(s)", Count ) );
			 return true;
		 },
		 { { "object", "Object id. Without it, shows every object that has a state machine or a behavior tree." } }, CategoryAI );

	Add( "ai.parts", "Write the catalog of the registered AI parts (names, categories, parameter schemas) for the editor executable (Editor.exe)",
		 []( FConsoleContext& Context, TOptional<FString> Path )
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr )
			 {
				 Context.Error( "no asset subsystem" );
				 return false;
			 }
			 const FString Relative = Path.IsSet() ? Path.GetValue() : FString( NAI::DefaultPartsCatalogPath );
			 if ( !NAI::SavePartsCatalog( *Assets, Relative ) )
			 {
				 Context.Error( "could not write '" + Relative + "'" );
				 return false;
			 }
			 Context.Print( "wrote the parts catalog: " + Relative + " (under the asset folder)" );
			 return true;
		 },
		 { { "path", "Path under the asset folder. Default: AI/AIParts.gxparts." } }, CategoryAI );

	Add( "ai.event", "Send an event to the state machine of an object",
		 []( FConsoleContext& Context, int64 ObjectId, const FString& Event )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( ObjectId );
			 CStateMachineComponent* Machine = Object.IsValid() ? Object->GetComponent<CStateMachineComponent>() : nullptr;
			 if ( Machine == nullptr )
			 {
				 Context.Error( "the object has no state machine" );
				 return false;
			 }
			 Machine->SendEvent( Event );
			 Context.Print( "sent '" + Event + "'" );
			 return true;
		 },
		 { { "object", "Object id." }, { "event", "Event name (a transition's On / Event condition)." } }, CategoryAI );

	Add( "profiler.capture", "Record a number of frames, then stop",
		 []( FConsoleContext& Context, int32 Frames )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( Frames < 1 )
			 {
				 Context.Error( "frames must be 1 or more" );
				 return false;
			 }
			 Profiler->CaptureFrames( Frames );
			 Context.Print( FString::Printf( "recording %d frame(s)", Frames ) );
			 return true;
		 },
		 { { "frames", "How many frames to record." } }, CategoryProfiler );

	Add( "profiler.clear", "Throw away the recorded profiler frames",
		 []( FConsoleContext& Context )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 Profiler->Clear();
			 Context.Print( "cleared" );
			 return true;
		 },
		 {}, CategoryProfiler );

	Add( "profiler.stats", "Show how much the profiler has recorded",
		 []()
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%s, %d frame(s), %lld sample(s), %s stored (limit %d MB%s), overhead ~%.0f ns per sample", Profiler->IsEnabled() ? "recording" : "off", Profiler->GetNumFrames(),
									 static_cast<long long>( Profiler->GetNumStoredSamples() ), *FormatBytes( Profiler->GetStoredBytes() ), Profiler->GetMemoryLimitMB(), Profiler->IsMemoryLimitReached() ? ", REACHED" : "",
									 Profiler->GetOverheadNsPerSample() );
		 },
		 {}, CategoryProfiler );

	Add( "profiler.save", "Save the recorded frames as a Chrome trace (open it at ui.perfetto.dev)",
		 []( FConsoleContext& Context, TOptional<FString> Path )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( Profiler->GetNumFrames() == 0 )
			 {
				 Context.Error( "nothing is recorded (use 'profiler on' first)" );
				 return false;
			 }
			 FString Target = Path.IsSet() && !Path.GetValue().IsEmpty() ? Path.GetValue() : NProfilerExport::MakeDefaultTracePath();
			 Target = Target.Replace( "\\", "/" );
			 const int32 Slash = Target.FindLast( "/" );
			 if ( Slash > 0 ) NFileSystem::EnsureDirectory( Target.Left( Slash ) );
			 if ( !NProfilerExport::WriteChromeTrace( *Profiler, 0, -1, Target ) )
			 {
				 Context.Error( "could not write " + Target );
				 return false;
			 }
			 Context.Print( "saved " + Target );
			 return true;
		 },
		 { { "path", "Output .json file. Default: Saved/Profiler/<project>_<date and time>.json" } }, CategoryProfiler );
}
