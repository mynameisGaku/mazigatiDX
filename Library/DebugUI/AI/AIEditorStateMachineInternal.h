// SPDX-License-Identifier: MIT
#pragma once
#include "AIEditorInternal.h"
#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/AI/AIGraphLayout.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// AI エディタのステートマシンのタブの中で、ファイルをまたいで使うもの（AIEditorStateMachine*.cpp 専用）
//=============================================================================
namespace NAIEditorStateMachineUI
{
	using FState = FAIEditorState;
	inline const ImU32 ColorText = IM_COL32( 235, 235, 240, 255 );
	inline const ImU32 ColorDimText = IM_COL32( 150, 152, 160, 255 );
	inline const ImU32 ColorBorder = IM_COL32( 95, 98, 108, 255 );
	inline const ImU32 ColorNodeFill = IM_COL32( 46, 48, 58, 255 );
	inline const ImU32 ColorContainerFill = IM_COL32( 36, 38, 48, 255 );
	inline const ImU32 ColorAccent = IM_COL32( 110, 190, 255, 255 );
	inline const ImU32 ColorDrop = IM_COL32( 110, 255, 140, 255 );
	inline const ImU32 ColorBad = IM_COL32( 255, 120, 110, 255 );
	inline const ImU32 ColorArrow = IM_COL32( 130, 134, 148, 255 );
	inline const ImU32 ColorAnyArrow = IM_COL32( 170, 125, 80, 255 );
	inline const ImU32 ColorAnyFill = IM_COL32( 70, 52, 34, 255 );
	inline const ImU32 ColorOrange = IM_COL32( 255, 165, 80, 255 );
	inline const ImVec4 InspectorAccent( 0.55f, 0.78f, 1.0f, 1.0f );

	// ---- AIEditorStateMachine.cpp ----
	ImVec2 Add( const ImVec2& A, const ImVec2& B );
	bool IsValidState( const FStateMachineDefinition& Definition, int32 State );
	void Field( const char* Name );
	const char* ConditionTypeName( EStateConditionType Type );
	FString DescribeCondition( const FStateConditionDef& Condition );
	FString DescribeTransition( const FStateTransitionDef& Transition );
	const TArray<FStateTransitionDef>& TransitionList( const FStateMachineDefinition& Definition, int32 Machine, bool bAny );

	// 定義の遷移を、グラフの配置の入力（FStateTransitionInfo）にする。Refs[Id] が、その遷移の場所。先が見つからない遷移は含めない
	struct FTransitionList
	{
		TArray<FStateTransitionInfo> Infos;
		TArray<FAIEditorTransitionRef> Refs;
	};

	// ---- AIEditorStateMachine.cpp ----
	FTransitionList BuildTransitionList( const FStateMachineDefinition& Definition );
	bool SameRef( const FAIEditorTransitionRef& A, const FAIEditorTransitionRef& B );
	TArray<int32> Siblings( const FStateMachineDefinition& Definition, int32 State );
	void SelectTransition( FState& S, int32 Machine, bool bAny, int32 Index );
	int32 FindTransition( const FStateMachineDefinition& Definition, int32 Machine, bool bAny, const FString& From, const FString& To );
	void SelectRoute( FState& S, const FTransitionList& Transitions, const FStateGraphRoute& Route, bool bCycle );
	void AddTransitionTo( FState& S, int32 State, int32 Target );
	void AddAnyTransitionTo( FState& S, int32 State );
	void DeleteState( FState& S, int32 State );
	void DeleteTransition( FState& S );

	// ---- AIEditorStateMachineGraph.cpp ----
	void DrawGraph( FState& S );

	// ---- AIEditorStateMachineInspector.cpp ----
	const TArray<FAICall>& ActionsOf( const FStateDef& State, EStateActionList List );
	void DrawActionList( FState& S, int32 StateIndex, EStateActionList List, const char* Title );
	void DrawStateInspector( FState& S );
	void DrawTransitionInspector( FState& S );
	void DrawInspector( FState& S );
}
