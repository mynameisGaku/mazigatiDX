// SPDX-License-Identifier: MIT
#include "AIDebugger.h"

#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/GameInstance/GameInstance.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/MathUtils.h"

namespace
{
	const ImVec4 ColorActive( 0.45f, 1.0f, 0.55f, 1.0f );
	const ImVec4 ColorRunning( 1.0f, 0.88f, 0.35f, 1.0f );
	const ImVec4 ColorSuccess( 0.55f, 0.85f, 0.6f, 1.0f );
	const ImVec4 ColorFailure( 1.0f, 0.5f, 0.45f, 1.0f );
	const ImVec4 ColorAborted( 1.0f, 0.65f, 0.3f, 1.0f );
	const ImVec4 ColorDim( 0.6f, 0.6f, 0.65f, 1.0f );
	const ImVec4 ColorChanged( 1.0f, 0.95f, 0.5f, 1.0f );

	CScene* FindCurrentScene()
	{
		CEngine* Engine = CEngine::Get();
		CGameInstance* GameInstance = Engine != nullptr ? Engine->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSceneManager().GetCurrentScene() : nullptr;
	}

	// AI の部品を持つオブジェクトの一覧
	TArray<TSharedPtr<CGameObject>> FindAIObjects( CScene& Scene )
	{
		TArray<TSharedPtr<CGameObject>> Result;
		for ( const TSharedPtr<CGameObject>& Object : Scene.GetObjects() )
		{
			if ( Object.IsValid() && ( Object->GetComponent<CStateMachineComponent>() != nullptr || Object->GetComponent<CBehaviorTreeComponent>() != nullptr ) )
			{
				Result.Add( Object );
			}
		}
		return Result;
	}

	FString ObjectLabel( const CGameObject& Object )
	{
		FString Kinds;
		if ( const_cast<CGameObject&>( Object ).GetComponent<CStateMachineComponent>() != nullptr )
		{
			Kinds += "FSM";
		}
		if ( const_cast<CGameObject&>( Object ).GetComponent<CBehaviorTreeComponent>() != nullptr )
		{
			Kinds += Kinds.IsEmpty() ? "BT" : "+BT";
		}
		return FString::Printf( "%s  [%s]", *Object.GetDebugName(), *Kinds );
	}

	ImVec4 SeverityColor( EAIIssueSeverity Severity )
	{
		switch ( Severity )
		{
		case EAIIssueSeverity::Info:
			return ColorDim;
		case EAIIssueSeverity::Warning:
			return ColorAborted;
		case EAIIssueSeverity::Error:
			return ColorFailure;
		}
		return ColorDim;
	}

	const char* SeverityLabel( EAIIssueSeverity Severity )
	{
		return Severity == EAIIssueSeverity::Info ? "info" : ( Severity == EAIIssueSeverity::Warning ? "warning" : "error" );
	}

	void DrawIssues( const char* Title, const TArray<FAIIssue>& Issues )
	{
		if ( Issues.IsEmpty() )
		{
			return;
		}
		if ( ImGui::CollapsingHeader( *FString::Printf( "%s issues (%d)##%s", Title, Issues.Num(), Title ) ) )
		{
			for ( const FAIIssue& Issue : Issues )
			{
				ImGui::TextColored( SeverityColor( Issue.Severity ), "%s", SeverityLabel( Issue.Severity ) );
				ImGui::SameLine();
				ImGui::TextWrapped( "%s: %s", UI_TEXT( Issue.Where ), UI_TEXT( Issue.Message ) );
			}
		}
	}

	//=========================================================================
	// ステートマシン
	//=========================================================================
	void DrawStateTree( const CStateMachine& Machine, int32 Parent, int32 Depth )
	{
		const FStateMachineDefinition& Definition = Machine.GetDefinition();
		const TArray<int32>& Active = Machine.GetActivePath();
		for ( const int32 Index : Definition.GetChildren( Parent ) )
		{
			const FStateDef& State = Definition.States[Index];
			const int32 ActiveDepth = Active.Find( Index );
			const bool bActive = ActiveDepth != INDEX_NONE;
			const bool bLeaf = !State.bMachine;

			ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if ( bLeaf )
			{
				Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			else if ( bActive )
			{
				Flags |= ImGuiTreeNodeFlags_DefaultOpen;
			}
			if ( bActive )
			{
				ImGui::PushStyleColor( ImGuiCol_Text, ColorActive );
			}
			else
			{
				ImGui::PushStyleColor( ImGuiCol_Text, ColorDim );
			}
			FString Label = State.Name;
			if ( !State.Clip.IsEmpty() )
			{
				Label += FString::Printf( "  (clip %s)", *State.Clip );
			}
			if ( bActive )
			{
				Label += FString::Printf( "  %.2f s", static_cast<float64>( Machine.GetStateTime( ActiveDepth ) ) );
			}
			const bool bOpen = ImGui::TreeNodeEx( reinterpret_cast<void*>( static_cast<intptr_t>( Index ) ), Flags, "%s", UI_TEXT( Label ) );
			ImGui::PopStyleColor();
			if ( bOpen && !bLeaf )
			{
				DrawStateTree( Machine, Index, Depth + 1 );
				ImGui::TreePop();
			}
		}
	}

	void DrawStateMachinePanel( FAIDebuggerState& State, CStateMachineComponent& Component )
	{
		const CStateMachine* Machine = Component.GetStateMachine();
		ImGui::TextUnformatted( "Asset:" );
		ImGui::SameLine();
		ImGui::TextColored( ColorDim, "%s", UI_TEXT( Component.GetPath() ) );
		if ( Machine == nullptr )
		{
			ImGui::TextColored( ColorFailure, "Not loaded (see the issues below)." );
			DrawIssues( "State machine", Component.GetIssues() );
			return;
		}

		// 今のステート
		ImGui::TextUnformatted( Machine->IsRunning() ? "State:" : "Stopped. Last state:" );
		ImGui::SameLine();
		ImGui::TextColored( ColorActive, "%s", UI_TEXT( Machine->DescribeActiveState() ) );
		ImGui::SameLine();
		ImGui::TextColored( ColorDim, "(%.2f s)", static_cast<float64>( Machine->GetLeafStateTime() ) );
		if ( Machine->HasHitTransitionLimit() )
		{
			ImGui::TextColored( ColorAborted, "Hit the transition limit in one tick at least once." );
		}

		// 操作
		if ( ImGui::Button( Component.IsPaused() ? "Resume##fsm" : "Pause##fsm" ) )
		{
			Component.SetPaused( !Component.IsPaused() );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Step##fsm" ) )
		{
			Component.Step( State.StepSeconds );
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 110.0f );
		ImGui::InputText( "##event", State.EventName, sizeof( State.EventName ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Send event" ) && State.EventName[0] != '\0' )
		{
			Component.SendEvent( State.EventName );
		}
		if ( Component.IsPaused() )
		{
			ImGui::TextColored( ColorRunning, "Paused. Step runs one tick of %.1f ms.", static_cast<float64>( State.StepSeconds ) * 1000.0 );
		}

		if ( ImGui::CollapsingHeader( "Graph##fsm", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			NDebugUI::DrawStateMachineGraph( State.Graph, Component );
		}
		if ( ImGui::CollapsingHeader( "States (list)" ) )
		{
			DrawStateTree( *Machine, -1, 0 );
		}

		if ( ImGui::CollapsingHeader( "Possible transitions", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			TArray<FStateTransitionCandidate> Candidates;
			Component.GetCandidateTransitions( Candidates );
			if ( Candidates.IsEmpty() )
			{
				ImGui::TextColored( ColorDim, "(none)" );
			}
			for ( const FStateTransitionCandidate& Candidate : Candidates )
			{
				ImGui::TextColored( Candidate.bConditionsMet ? ColorActive : ColorDim, "%s", Candidate.bConditionsMet ? "READY" : "wait " );
				ImGui::SameLine();
				ImGui::TextColored( Candidate.bConditionsMet ? ColorActive : ColorDim, "%s%s", UI_TEXT( Candidate.Description ), Candidate.Priority != 0 ? *FString::Printf( "  [priority %d]", Candidate.Priority ) : "" );
			}
		}

		if ( ImGui::CollapsingHeader( "History (oldest first)" ) )
		{
			TArray<FStateHistoryEntry> History;
			Machine->GetHistory( History );
			for ( const FStateHistoryEntry& Entry : History )
			{
				const char* Cause = Entry.Cause == EStateTransitionCause::Initial ? "start" : ( Entry.Cause == EStateTransitionCause::AnyTransition ? "any  " : "trans" );
				ImGui::TextColored( ColorDim, "%7.2f s  %s", Entry.Time, Cause );
				ImGui::SameLine();
				ImGui::TextUnformatted( UI_TEXT( Entry.Description ) );
			}
		}
		DrawIssues( "State machine", Component.GetIssues() );
	}

	//=========================================================================
	// ビヘイビアツリー
	//=========================================================================
	void DrawBehaviorNode( const CBehaviorTree& Tree, int32 Node, double Now, double TreeTime )
	{
		const FBehaviorTreeDefinition& Definition = Tree.GetDefinition();
		const FBTNodeDef& Def = Definition.Nodes[Node];
		const bool bActive = Tree.IsNodeActive( Node );
		const bool bHasResult = Tree.HasNodeResult( Node );
		const TArray<int32> Children = Definition.GetChildren( Node );
		const bool bLeaf = Children.IsEmpty() && Def.Decorators.IsEmpty() && Def.Services.IsEmpty();

		// 色: 動いている = 緑、直近の結果で 成功 = 薄い緑・失敗 = 赤・中断 = オレンジ、まだ動いたことがない = 灰色
		ImVec4 Color = ColorDim;
		FString Status;
		if ( bActive )
		{
			Color = Def.Type == EBTNodeType::Task ? ColorRunning : ColorActive;
			Status = "ACTIVE";
		}
		else if ( bHasResult )
		{
			const EBTStatus Result = Tree.GetNodeResult( Node );
			Color = Tree.WasNodeAborted( Node ) ? ColorAborted : ( Result == EBTStatus::Success ? ColorSuccess : ColorFailure );
			Status = FString::Printf( "%s%s %.1f s ago", Tree.WasNodeAborted( Node ) ? "aborted " : "", Result == EBTStatus::Success ? "success" : "failure", NMath::Max( TreeTime - Tree.GetNodeResultTime( Node ), 0.0 ) );
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if ( bLeaf )
		{
			Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		ImGui::PushStyleColor( ImGuiCol_Text, Color );
		FString Label = Definition.GetNodeLabel( Node );
		if ( Def.Type == EBTNodeType::Task )
		{
			Label += FString::Printf( "  <%s>", *Def.Task.Do );
		}
		else
		{
			Label += FString::Printf( "  [%s%s]", NAI::ToString( Def.Type ), Def.Type == EBTNodeType::Parallel ? *FString::Printf( " %s", NAI::ToString( Def.ParallelMode ) ) : "" );
		}
		if ( !Status.IsEmpty() )
		{
			Label += "   " + Status;
		}
		const bool bOpen = ImGui::TreeNodeEx( reinterpret_cast<void*>( static_cast<intptr_t>( Node ) ), Flags, "%s", UI_TEXT( Label ) );
		ImGui::PopStyleColor();
		if ( !bOpen )
		{
			return;
		}
		if ( !bLeaf )
		{
			for ( int32 Index = 0; Index < Def.Decorators.Num(); ++Index )
			{
				const FBTDecoratorDef& Decorator = Def.Decorators[Index];
				const bool bIsCondition = Decorator.Type == EBTDecoratorType::BlackboardCondition || Decorator.Type == EBTDecoratorType::Condition;
				// 条件の真偽を先に出す（長い説明が、ウィンドウの端で切れても見えるように）
				if ( bIsCondition )
				{
					const bool bValue = Tree.GetDecoratorResult( Node, Index );
					ImGui::TextColored( bValue ? ColorActive : ColorFailure, "  [%s]", bValue ? "true " : "false" );
				}
				else
				{
					ImGui::TextColored( ColorDim, "  [     ]" );
				}
				ImGui::SameLine();
				ImGui::TextUnformatted( UI_TEXT( NDebugUI::DescribeBehaviorDecorator( Decorator ) ) );
			}
			for ( const FBTServiceDef& Service : Def.Services )
			{
				ImGui::TextColored( ColorDim, "  service" );
				ImGui::SameLine();
				ImGui::Text( "%s every %g s", UI_TEXT( Service.Call.Do ), static_cast<float64>( Service.Interval ) );
			}
			for ( const int32 Child : Children )
			{
				DrawBehaviorNode( Tree, Child, Now, TreeTime );
			}
			ImGui::TreePop();
		}
	}

	void DrawBehaviorTreePanel( FAIDebuggerState& State, CBehaviorTreeComponent& Component, double Now )
	{
		const CBehaviorTree* Tree = Component.GetBehaviorTree();
		ImGui::TextUnformatted( "Asset:" );
		ImGui::SameLine();
		ImGui::TextColored( ColorDim, "%s", UI_TEXT( Component.GetPath() ) );
		if ( Tree == nullptr )
		{
			ImGui::TextColored( ColorFailure, "Not loaded (see the issues below)." );
			DrawIssues( "Behavior tree", Component.GetIssues() );
			return;
		}

		const EBTStatus Root = Tree->GetRootResult();
		ImGui::TextUnformatted( Component.IsRunning() ? "Root:" : "Stopped. Root:" );
		ImGui::SameLine();
		ImGui::TextColored( Root == EBTStatus::Running ? ColorRunning : ( Root == EBTStatus::Success ? ColorSuccess : ColorFailure ), "%s", Root == EBTStatus::Running ? "running" : ( Root == EBTStatus::Success ? "success" : "failure" ) );
		ImGui::SameLine();
		ImGui::TextColored( ColorDim, "(%d node(s) visited last tick)", Tree->GetLastNumVisits() );
		if ( Tree->HasHitVisitLimit() )
		{
			ImGui::TextColored( ColorAborted, "Hit the node limit in one tick at least once." );
		}

		if ( ImGui::Button( Component.IsPaused() ? "Resume##bt" : "Pause##bt" ) )
		{
			Component.SetPaused( !Component.IsPaused() );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Step##bt" ) )
		{
			Component.Step( State.StepSeconds );
		}
		if ( Component.IsPaused() )
		{
			ImGui::SameLine();
			ImGui::TextColored( ColorRunning, "Paused. Step runs one tick of %.1f ms.", static_cast<float64>( State.StepSeconds ) * 1000.0 );
		}

		if ( ImGui::CollapsingHeader( "Graph##bt", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			NDebugUI::DrawBehaviorTreeGraph( State.Graph, Component );
		}
		if ( ImGui::CollapsingHeader( "Nodes (list)" ) )
		{
			// 結果の時刻はコンポーネントの時間（FAIContext::Time）なので、「何秒前」は、コンポーネントの今の時間から数える
			DrawBehaviorNode( *Tree, 0, Now, Component.GetTime() );
		}
		DrawIssues( "Behavior tree", Component.GetIssues() );
	}

	//=========================================================================
	// ブラックボード
	//=========================================================================
	FString ValueToText( const CBlackboard& Blackboard, FBlackboardKey Key )
	{
		switch ( Blackboard.GetKeyType( Key ) )
		{
		case EBlackboardKeyType::Bool:
			return Blackboard.GetBool( Key ) ? "true" : "false";
		case EBlackboardKeyType::Int:
			return FString::Printf( "%d", Blackboard.GetInt( Key ) );
		case EBlackboardKeyType::Float:
			return FString::Printf( "%g", static_cast<float64>( Blackboard.GetFloat( Key ) ) );
		case EBlackboardKeyType::Vector3:
		{
			const FVector3 Vector = Blackboard.GetVector( Key );
			return FString::Printf( "(%g, %g, %g)", static_cast<float64>( Vector.x ), static_cast<float64>( Vector.y ), static_cast<float64>( Vector.z ) );
		}
		case EBlackboardKeyType::Name:
			return "\"" + Blackboard.GetName( Key ) + "\"";
		case EBlackboardKeyType::Object:
			return FString::Printf( "#%llu", static_cast<unsigned long long>( Blackboard.GetObjectId( Key ) ) );
		case EBlackboardKeyType::Enum:
			return Blackboard.GetEnumName( Key );
		}
		return FString();
	}

	// 前回から変わったキーを、履歴に足す（最初の観測は、記録しない）
	void ObserveChanges( FAIDebuggerState& State, const CBlackboard& Blackboard, double Now )
	{
		const int32 NumKeys = Blackboard.GetNumKeys();
		if ( State.ObservedBlackboard != &Blackboard || State.LastRevisions.Num() != NumKeys )
		{
			State.ObservedBlackboard = &Blackboard;
			State.Changes.Empty();
			State.LastRevisions.SetNum( NumKeys );
			State.LastChangeTimes.SetNum( NumKeys );
			for ( int32 Index = 0; Index < NumKeys; ++Index )
			{
				State.LastRevisions[Index] = Blackboard.GetRevision( FBlackboardKey( Index ) );
				State.LastChangeTimes[Index] = -1000.0;
			}
			return;
		}
		for ( int32 Index = 0; Index < NumKeys; ++Index )
		{
			const FBlackboardKey Key( Index );
			const uint32 Revision = Blackboard.GetRevision( Key );
			if ( Revision != State.LastRevisions[Index] )
			{
				State.LastRevisions[Index] = Revision;
				State.LastChangeTimes[Index] = Now;
				FAIDebuggerState::FChange Change;
				Change.Time = Now;
				Change.Text = FString::Printf( "%s = %s", *Blackboard.GetKeyName( Key ), *ValueToText( Blackboard, Key ) );
				State.Changes.Add( Change );
				if ( State.Changes.Num() > 40 )
				{
					State.Changes.RemoveAt( 0 );
				}
			}
		}
	}

	void DrawBlackboardPanel( FAIDebuggerState& State, CBlackboard& Blackboard, CScene* Scene, double Now )
	{
		ObserveChanges( State, Blackboard, Now );
		if ( Blackboard.GetNumKeys() == 0 )
		{
			ImGui::TextColored( ColorDim, "(no keys)" );
			return;
		}
		if ( ImGui::BeginTable( "##blackboard", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp ) )
		{
			ImGui::TableSetupColumn( "Key", 0, 1.6f );
			ImGui::TableSetupColumn( "Type", 0, 0.9f );
			ImGui::TableSetupColumn( "Value (click to edit)", 0, 2.2f );
			ImGui::TableSetupColumn( "Rev", 0, 0.5f );
			ImGui::TableHeadersRow();
			for ( int32 Index = 0; Index < Blackboard.GetNumKeys(); ++Index )
			{
				const FBlackboardKey Key( Index );
				const bool bFlash = Now - State.LastChangeTimes[Index] < 0.8;
				ImGui::TableNextRow();
				ImGui::PushID( Index );
				ImGui::TableNextColumn();
				ImGui::TextColored( bFlash ? ColorChanged : ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ), "%s", UI_TEXT( Blackboard.GetKeyName( Key ) ) );
				ImGui::TableNextColumn();
				const bool bTrigger = Blackboard.GetDefinition().Keys[Index].bTrigger;
				ImGui::TextColored( ColorDim, "%s%s", NBlackboard::ToString( Blackboard.GetKeyType( Key ) ), bTrigger ? " (trigger)" : "" );
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth( -FLT_MIN );
				switch ( Blackboard.GetKeyType( Key ) )
				{
				case EBlackboardKeyType::Bool:
				{
					bool bValue = Blackboard.GetBool( Key );
					if ( ImGui::Checkbox( "##v", &bValue ) )
					{
						Blackboard.SetBool( Key, bValue );
					}
					break;
				}
				case EBlackboardKeyType::Int:
				{
					int32 Value = Blackboard.GetInt( Key );
					if ( ImGui::DragInt( "##v", &Value ) )
					{
						Blackboard.SetInt( Key, Value );
					}
					break;
				}
				case EBlackboardKeyType::Float:
				{
					float32 Value = Blackboard.GetFloat( Key );
					if ( ImGui::DragFloat( "##v", &Value, 0.05f ) )
					{
						Blackboard.SetFloat( Key, Value );
					}
					break;
				}
				case EBlackboardKeyType::Vector3:
				{
					const FVector3 Current = Blackboard.GetVector( Key );
					float32 Values[3] = { Current.x, Current.y, Current.z };
					if ( ImGui::DragFloat3( "##v", Values, 0.05f ) )
					{
						Blackboard.SetVector( Key, FVector3( Values[0], Values[1], Values[2] ) );
					}
					break;
				}
				case EBlackboardKeyType::Name:
				{
					FString Value = Blackboard.GetName( Key );
					if ( NDebugUI::InputString( "##v", Value, ImGuiInputTextFlags_EnterReturnsTrue ) )
					{
						Blackboard.SetName( Key, Value );
					}
					break;
				}
				case EBlackboardKeyType::Object:
				{
					const uint64 Id = Blackboard.GetObjectId( Key );
					const TSharedPtr<CGameObject> Object = Scene != nullptr && Id != 0 ? Scene->FindObjectById( Id ) : TSharedPtr<CGameObject>();
					ImGui::Text( "#%llu %s", static_cast<unsigned long long>( Id ), Object.IsValid() ? UI_TEXT( Object->GetDebugName() ) : ( Id == 0 ? "(none)" : "(gone)" ) );
					break;
				}
				case EBlackboardKeyType::Enum:
				{
					const TArray<FString>& Options = Blackboard.GetDefinition().Keys[Index].EnumValues;
					const FString Current = Blackboard.GetEnumName( Key );
					if ( ImGui::BeginCombo( "##v", UI_TEXT( Current ) ) )
					{
						for ( int32 Choice = 0; Choice < Options.Num(); ++Choice )
						{
							if ( ImGui::Selectable( UI_TEXT( Options[Choice] ), Choice == Blackboard.GetEnum( Key ) ) )
							{
								Blackboard.SetEnum( Key, Choice );
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
				}
				ImGui::TableNextColumn();
				ImGui::TextColored( ColorDim, "%u", Blackboard.GetRevision( Key ) );
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if ( ImGui::CollapsingHeader( *FString::Printf( "Recent changes (%d)###changes", State.Changes.Num() ) ) )
		{
			if ( State.Changes.IsEmpty() )
			{
				ImGui::TextColored( ColorDim, "(nothing changed since you selected this object)" );
			}
			for ( int32 Index = State.Changes.Num() - 1; Index >= 0; --Index )
			{
				ImGui::TextColored( ColorDim, "%7.1f s ago", Now - State.Changes[Index].Time );
				ImGui::SameLine();
				ImGui::TextUnformatted( UI_TEXT( State.Changes[Index].Text ) );
			}
		}
	}
}

void NDebugUI::DrawAIDebugger( FAIDebuggerState& State, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const float Width = NMath::Min( 760.0f, MainViewport->Size.x - 40.0f );
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + NMath::Max( 20.0f, MainViewport->Size.x - Width - 20.0f ), MainViewport->Pos.y + 44.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( Width, NMath::Min( 900.0f, MainViewport->Size.y - 60.0f ) ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "AI Debugger", pOpen ) )
	{
		ImGui::End();
		return;
	}

	CScene* Scene = FindCurrentScene();
	if ( Scene == nullptr )
	{
		ImGui::TextUnformatted( "There is no scene." );
		ImGui::End();
		return;
	}
	const TArray<TSharedPtr<CGameObject>> Objects = FindAIObjects( *Scene );
	if ( Objects.IsEmpty() )
	{
		ImGui::TextWrapped( "No object in this scene has a state machine or a behavior tree. Add CStateMachineComponent / CBehaviorTreeComponent to an object." );
		ImGui::End();
		return;
	}

	// 選んでいるオブジェクト（消えた・まだ選んでいないときは、最初の候補）
	if ( State.SelectedObjectId == 0 && !State.InitialSelection.IsEmpty() )
	{
		for ( const TSharedPtr<CGameObject>& Object : Objects )
		{
			if ( State.SelectedObjectId == 0 && Object->GetDebugName().Contains( State.InitialSelection, ESearchCase::IgnoreCase ) )
			{
				State.SelectedObjectId = Object->GetId();
			}
		}
		State.InitialSelection.Empty();
	}
	TSharedPtr<CGameObject> Selected;
	for ( const TSharedPtr<CGameObject>& Object : Objects )
	{
		if ( Object->GetId() == State.SelectedObjectId )
		{
			Selected = Object;
		}
	}
	if ( !Selected.IsValid() )
	{
		Selected = Objects[0];
		State.SelectedObjectId = Selected->GetId();
	}
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 215.0f );
	if ( ImGui::BeginCombo( "Object", UI_TEXT( ObjectLabel( *Selected ) ) ) )
	{
		for ( const TSharedPtr<CGameObject>& Object : Objects )
		{
			if ( ImGui::Selectable( UI_TEXT( ObjectLabel( *Object ) ), Object->GetId() == State.SelectedObjectId ) )
			{
				State.SelectedObjectId = Object->GetId();
				Selected = Object;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 60.0f );
	ImGui::DragFloat( "Step (s)", &State.StepSeconds, 0.001f, 0.001f, 1.0f, "%.3f" );
	ImGui::SetNextItemWidth( 160.0f );
	ImGui::SliderFloat( "Graph height", &State.Graph.Height, 160.0f, 700.0f, "%.0f" );

	const double Now = ImGui::GetTime();
	CStateMachineComponent* Machine = Selected->GetComponent<CStateMachineComponent>();
	CBehaviorTreeComponent* Brain = Selected->GetComponent<CBehaviorTreeComponent>();
	if ( Machine != nullptr && ImGui::CollapsingHeader( "State machine", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		DrawStateMachinePanel( State, *Machine );
	}
	if ( Brain != nullptr && ImGui::CollapsingHeader( "Behavior tree", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		DrawBehaviorTreePanel( State, *Brain, Now );
	}
	const TSharedPtr<CBlackboard> Blackboard = Machine != nullptr && Machine->GetBlackboard().IsValid() ? Machine->GetBlackboard() : ( Brain != nullptr ? Brain->GetBlackboard() : TSharedPtr<CBlackboard>() );
	if ( Blackboard.IsValid() && ImGui::CollapsingHeader( "Blackboard", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		DrawBlackboardPanel( State, *Blackboard, Scene, Now );
	}
	ImGui::End();
}
