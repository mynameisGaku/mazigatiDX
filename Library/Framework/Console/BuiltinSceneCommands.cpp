// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: シーン・オブジェクト・ECS
void NConsole::NBuiltin::RegisterSceneCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "scene", "Show the current scene, or change to another registered scene",
		 []( FConsoleContext& Context, TOptional<FString> Name )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 if ( !Name.IsSet() )
			 {
				 const CScene* Scene = Manager->GetCurrentScene();
				 Context.Print( Scene != nullptr ? "current scene: " + Scene->GetName() + FString::Printf( " (%d object(s))", Scene->GetNumObjects() ) : FString( "no scene is running" ) );
				 return true;
			 }
			 if ( !Manager->IsSceneRegistered( Name.GetValue() ) )
			 {
				 Context.Error( "scene '" + Name.GetValue() + "' is not registered (see 'scenes')" );
				 return false;
			 }
			 Manager->ChangeScene( Name.GetValue() );
			 Context.Print( "changing to " + Name.GetValue() + " at the start of the next frame" );
			 return true;
		 },
		 { { "name", "A registered scene name (see 'scenes'). Without it, shows the current scene." } }, CategoryScene );
	Registry.SetArgCompleter( "scene", 0, []() { return GetSceneNames(); } );

	Add( "scenes", "List the registered scenes and the scenes on the stack",
		 []( FConsoleContext& Context )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 Context.Print( "registered: " + FString::Join( GetSceneNames(), ", " ) );
			 FString Stack;
			 for ( const TUniquePtr<CScene>& Scene : Manager->GetSceneStack() ) Stack += ( Stack.IsEmpty() ? "" : " > " ) + Scene->GetName();
			 Context.Print( "stack (bottom to top): " + ( Stack.IsEmpty() ? FString( "(empty)" ) : Stack ) );
			 return true;
		 },
		 {}, CategoryScene );

	Add( "push", "Push a registered scene on top of the current one (the scene below pauses)",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 if ( !Manager->IsSceneRegistered( Name ) )
			 {
				 Context.Error( "scene '" + Name + "' is not registered (see 'scenes')" );
				 return false;
			 }
			 Manager->PushScene( Name );
			 return true;
		 },
		 { "name" }, CategoryScene );
	Registry.SetArgCompleter( "push", 0, []() { return GetSceneNames(); } );

	Add( "pop", "Remove the top scene (the scene below resumes)",
		 []()
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 Manager->PopScene();
			 return true;
		 },
		 {}, CategoryScene );

	Add( "reload", "Restart the current scene",
		 []( FConsoleContext& Context )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 const CScene* Scene = Manager != nullptr ? Manager->GetCurrentScene() : nullptr;
			 if ( Scene == nullptr || !Manager->IsSceneRegistered( Scene->GetName() ) )
			 {
				 Context.Error( "the current scene is not a registered scene" );
				 return false;
			 }
			 Manager->ChangeScene( Scene->GetName() );
			 Context.Print( "restarting " + Scene->GetName() + " at the start of the next frame" );
			 return true;
		 },
		 {}, CategoryScene );

	Add( "objects", "List the objects in the current scene (id, name, position, state)",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return false;
			 constexpr int32 MaxLines = 200;
			 int32 Matched = 0;
			 for ( const TSharedPtr<CGameObject>& Object : Scene->GetObjects() )
			 {
				 const FString Name = Object->GetDebugName();
				 if ( Filter.IsSet() && !Name.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 if ( ++Matched > MaxLines ) continue;
				 FString Line = FString::Printf( "  #%-5llu %s", static_cast<unsigned long long>( Object->GetId() ), *Name );
				 if ( const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>() ) Line += "  " + Vec3Text( Transform->GetWorldPosition() );
				 if ( !Object->IsVisible() ) Line += "  [hidden]";
				 if ( !Object->IsTickEnabled() ) Line += "  [no tick]";
				 if ( Object->IsPendingDestroy() ) Line += "  [destroying]";
				 Context.Print( Line );
			 }
			 if ( Matched > MaxLines ) Context.Print( FString::Printf( "  ... %d more (narrow it with a filter)", Matched - MaxLines ) );
			 Context.Print( FString::Printf( "%d object(s)%s", Matched, Filter.IsSet() ? " matched" : "" ) );
			 return true;
		 },
		 { { "filter", "Only the objects whose name contains this text." } }, CategoryObjects );

	Add( "object", "Show the details of one object: parent, children, transform and components",
		 []( FConsoleContext& Context, int64 Id )
		 {
			 if ( FindCurrentScene() == nullptr ) return false;
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Context.Print( Object->GetDebugName() + FString::Printf( "  (type %s)", *Object->GetTypeName() ) );
			 Context.Print( FString::Printf( "  visible %s, tick %s, draw order %d%s", Object->IsVisible() ? "yes" : "no", Object->IsTickEnabled() ? "on" : "off", Object->GetDrawOrder(),
											 Object->IsPendingDestroy() ? ", destroying" : "" ) );
			 if ( const CGameObject* Parent = Object->GetParent() ) Context.Print( "  parent: " + Parent->GetDebugName() );
			 for ( const CGameObject* Child : Object->GetChildren() ) Context.Print( "  child: " + Child->GetDebugName() );
			 if ( const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>() )
			 {
				 const FTransform& Local = Transform->GetLocalTransform();
				 const FVector3 Euler = Local.Rotation.ToEuler();
				 Context.Print( "  local position " + Vec3Text( Local.Position ) + ", rotation (deg) " + Vec3Text( FVector3( NMath::ToDegrees( Euler.x ), NMath::ToDegrees( Euler.y ), NMath::ToDegrees( Euler.z ) ) ) + ", scale " + Vec3Text( Local.Scale ) );
				 Context.Print( "  world position " + Vec3Text( Transform->GetWorldPosition() ) );
			 }
			 for ( int32 i = 0; i < Object->GetNumComponents(); ++i )
			 {
				 if ( const CComponent* Component = Object->GetComponentAt( i ) ) Context.Print( "  component: " + Component->GetTypeName() );
			 }
			 return true;
		 },
		 { { "id", "The object id shown by 'objects' (the number after #)." } }, CategoryObjects );

	Add( "destroy", "Destroy an object (and its children)",
		 []( FConsoleContext& Context, int64 Id )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Context.Print( "destroying " + Object->GetDebugName() );
			 Object->Destroy();
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." } }, CategoryObjects );

	Add( "visible", "Show or hide an object (its Draw is skipped while hidden)",
		 []( FConsoleContext& Context, int64 Id, TOptional<bool> On )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 const bool bShow = On.IsSet() ? On.GetValue() : !Object->IsVisible();
			 Object->SetVisible( bShow );
			 Context.Print( Object->GetDebugName() + ( bShow ? " is visible" : " is hidden" ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "on", "true to show, false to hide. Without it, switches." } }, CategoryObjects );

	Add( "tick", "Turn the Tick of an object on or off",
		 []( FConsoleContext& Context, int64 Id, TOptional<bool> On )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 const bool bEnable = On.IsSet() ? On.GetValue() : !Object->IsTickEnabled();
			 Object->SetTickEnabled( bEnable );
			 Context.Print( Object->GetDebugName() + ( bEnable ? ": tick on" : ": tick off" ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "on", "true to turn on, false to turn off. Without it, switches." } }, CategoryObjects );

	Add( "teleport", "Move an object to a world position",
		 []( FConsoleContext& Context, int64 Id, const FVector3& Position )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 CTransformComponent* Transform = Object.IsValid() ? Object->GetComponent<CTransformComponent>() : nullptr;
			 if ( Transform == nullptr )
			 {
				 Context.Error( FString::Printf( "object %lld does not exist or has no transform (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Transform->SetWorldPosition( Position );
			 Context.Print( Object->GetDebugName() + " -> " + Vec3Text( Transform->GetWorldPosition() ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "position", "World position: three numbers, x y z." } }, CategoryObjects );

	Add( "ecs", "Show the ECS statistics of the current scene (entities and systems)",
		 []()
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return FString( "no scene is running" );
			 const NEcs::FSystemScheduler& Systems = Scene->GetSystems();
			 return FString::Printf( "scene %s: %d entit(ies), %d object(s), %d system(s) (update stages: %d pre, %d post; draw: %d)", *Scene->GetName(), Scene->GetEcsWorld().GetNumEntities(), Scene->GetNumObjects(),
									 Systems.GetNumSystems(), Systems.GetNumStages( NEcs::EPhase::PreUpdate ), Systems.GetNumStages( NEcs::EPhase::PostUpdate ), Systems.GetNumStages( NEcs::EPhase::Draw ) );
		 },
		 {}, CategoryEcs );

	Add( "systems", "List the ECS systems of the current scene (phase and parallel stage)",
		 []( FConsoleContext& Context )
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return false;
			 const NEcs::FSystemScheduler& Systems = Scene->GetSystems();
			 const char* Phases[] = { "PreUpdate", "PostUpdate", "Draw" };
			 for ( int32 i = 0; i < Systems.GetNumSystems(); ++i )
			 {
				 Context.Print( FString::Printf( "  %-11s stage %d  %s", Phases[static_cast<int32>( Systems.GetSystemPhase( i ) )], Systems.GetSystemStage( i ), Systems.GetSystemName( i ) ) );
			 }
			 Context.Print( FString::Printf( "%d system(s)", Systems.GetNumSystems() ) );
			 return true;
		 },
		 {}, CategoryEcs );
}
