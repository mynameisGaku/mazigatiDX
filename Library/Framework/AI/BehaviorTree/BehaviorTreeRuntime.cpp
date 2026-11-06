// SPDX-License-Identifier: MIT
#include "BehaviorTreeRuntime.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"

namespace
{
	bool IsConditionDecorator( EBTDecoratorType Type )
	{
		return Type == EBTDecoratorType::BlackboardCondition || Type == EBTDecoratorType::Condition;
	}

	bool AbortsSelf( EBTAbortMode Mode )
	{
		return Mode == EBTAbortMode::Self || Mode == EBTAbortMode::Both;
	}

	bool AbortsLowerPriority( EBTAbortMode Mode )
	{
		return Mode == EBTAbortMode::LowerPriority || Mode == EBTAbortMode::Both;
	}
}

CBehaviorTree::~CBehaviorTree() = default;

TUniquePtr<CBehaviorTree> CBehaviorTree::Create( const TSharedPtr<const FBehaviorTreeDefinition>& Definition, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues )
{
	if ( !Definition.IsValid() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, FString(), "There is no behavior tree definition." );
		return nullptr;
	}
	TUniquePtr<CBehaviorTree> Tree( new CBehaviorTree() );
	Tree->Definition = Definition;
	if ( !Tree->Compile( Registry, Blackboard, Issues ) )
	{
		return nullptr;
	}
	return Tree;
}

//=============================================================================
// コンパイル
//=============================================================================
void CBehaviorTree::CompileDecorator( const FBTDecoratorDef& Def, FCompiledDecorator& Out, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues ) const
{
	Out.Type = Def.Type;
	Out.Abort = Def.Abort;
	Out.bNot = Def.bNot;
	Out.Seconds = Def.Seconds;
	Out.Count = Def.Count;
	Out.bUntilSuccess = Def.bUntilSuccess;
	Out.Op = Def.Op;

	if ( Def.Type == EBTDecoratorType::BlackboardCondition )
	{
		const int32 KeyIndex = Blackboard.FindKeyIndex( Def.Key );
		if ( KeyIndex < 0 )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The blackboard key '%s' does not exist. The condition is always false.", *Def.Key ) );
			Out.bValid = false;
			return;
		}
		Out.Key = FBlackboardKey( KeyIndex );
		Out.KeyType = Blackboard.Keys[KeyIndex].Type;
		FString Reason;
		if ( !NAI::PrepareCompare( Blackboard.Keys[KeyIndex], Def.Op, Def.Value, Out.Compare, Reason ) )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, Reason + " The condition is always false." );
			Out.bValid = false;
		}
	}
	else if ( Def.Type == EBTDecoratorType::Condition )
	{
		const CAIRegistry::FConditionEntry* Entry = Registry.FindCondition( Def.Call.Do );
		if ( Entry == nullptr )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The condition '%s' is not registered. The condition is always false.", *Def.Call.Do ) );
			Out.bValid = false;
			return;
		}
		Out.Custom = TSharedPtr<FAICondition>( Entry->Create() );
		Out.Custom->Initialize( NAI::ResolveParams( Entry->Schema, Def.Call.Params.is_null() ? nullptr : &Def.Call.Params, Where, Issues ) );
	}
}

bool CBehaviorTree::Compile( const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues )
{
	const FBehaviorTreeDefinition& Def = *Definition;
	if ( !Def.HasRoot() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, FString(), "The behavior tree has no root." );
		return false;
	}

	// 問題の場所（"Root/Attack/Wait #5" のような、根からの名前）
	const auto WhereOf = [&Def]( int32 Index )
	{
		FString Path = Def.GetNodeLabel( Index );
		for ( int32 Parent = Def.Nodes[Index].Parent, Depth = 0; Parent >= 0 && Depth < Def.Nodes.Num(); Parent = Def.Nodes[Parent].Parent, ++Depth )
		{
			Path = Def.GetNodeLabel( Parent ) + "/" + Path;
		}
		return Path;
	};

	Nodes.SetNum( Def.Nodes.Num() );
	for ( int32 Index = 0; Index < Def.Nodes.Num(); ++Index )
	{
		const FBTNodeDef& Node = Def.Nodes[Index];
		FCompiledNode& Compiled = Nodes[Index];
		const FString Where = WhereOf( Index );
		Compiled.Type = Node.Type;
		Compiled.Mode = Node.ParallelMode;
		Compiled.Parent = Node.Parent;
		Compiled.Children = Def.GetChildren( Index );
		if ( Node.Type == EBTNodeType::Parallel )
		{
			Compiled.ParallelResults.AddZeroed( Compiled.Children.Num() );
		}
		if ( Node.Type != EBTNodeType::Task && Compiled.Children.IsEmpty() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, Node.Type == EBTNodeType::Sequence ? "The Sequence has no children. It always succeeds." : "The composite has no children. It always fails." );
		}

		if ( Node.Type == EBTNodeType::Task )
		{
			const CAIRegistry::FTaskEntry* Entry = Registry.FindTask( Node.Task.Do );
			if ( Entry == nullptr )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The task '%s' is not registered. The node always fails.", *Node.Task.Do ) );
			}
			else
			{
				Compiled.Task = TSharedPtr<FAITask>( Entry->Create() );
				Compiled.Task->Initialize( NAI::ResolveParams( Entry->Schema, Node.Task.Params.is_null() ? nullptr : &Node.Task.Params, Where, Issues ) );
			}
		}

		const bool bParentIsSelector = Node.Parent >= 0 && Def.Nodes[Node.Parent].Type == EBTNodeType::Selector;
		for ( int32 DecoratorIndex = 0; DecoratorIndex < Node.Decorators.Num(); ++DecoratorIndex )
		{
			FCompiledDecorator Decorator;
			const FString DecoratorWhere = FString::Printf( "%s/Decorators[%d]", *Where, DecoratorIndex );
			CompileDecorator( Node.Decorators[DecoratorIndex], Decorator, Registry, Blackboard, DecoratorWhere, Issues );
			if ( AbortsLowerPriority( Decorator.Abort ) && !bParentIsSelector )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, DecoratorWhere, "LowerPriority only works on a child of a Selector. It was ignored." );
				Decorator.Abort = AbortsSelf( Decorator.Abort ) ? EBTAbortMode::Self : EBTAbortMode::None;
			}
			Compiled.Decorators.Add( Decorator );
		}
		for ( int32 ServiceIndex = 0; ServiceIndex < Node.Services.Num(); ++ServiceIndex )
		{
			const FBTServiceDef& ServiceDef = Node.Services[ServiceIndex];
			const FString ServiceWhere = FString::Printf( "%s/Services[%d]", *Where, ServiceIndex );
			const CAIRegistry::FServiceEntry* Entry = Registry.FindService( ServiceDef.Call.Do );
			if ( Entry == nullptr )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ServiceWhere, FString::Printf( "The service '%s' is not registered. It was skipped.", *ServiceDef.Call.Do ) );
				continue;
			}
			FCompiledService Service;
			Service.Part = TSharedPtr<FAIService>( Entry->Create() );
			Service.Part->Initialize( NAI::ResolveParams( Entry->Schema, ServiceDef.Call.Params.is_null() ? nullptr : &ServiceDef.Call.Params, ServiceWhere, Issues ) );
			Service.Interval = ServiceDef.Interval;
			Compiled.Services.Add( Service );
		}
	}
	return true;
}

//=============================================================================
// 条件
//=============================================================================
bool CBehaviorTree::EvaluateCondition( FCompiledDecorator& Decorator, FAIContext& Context ) const
{
	bool bResult = false;
	if ( Decorator.bValid )
	{
		if ( Decorator.Type == EBTDecoratorType::BlackboardCondition )
		{
			FBlackboardValue Current;
			if ( Context.Blackboard != nullptr && Context.Blackboard->GetValue( Decorator.Key, Current ) )
			{
				bResult = NAI::CompareValues( Decorator.KeyType, Current, Decorator.Op, Decorator.Compare );
				if ( Decorator.bNot )
				{
					bResult = !bResult;
				}
			}
			Decorator.ObservedRevision = Context.Blackboard != nullptr ? Context.Blackboard->GetRevision( Decorator.Key ) : 0;
		}
		else if ( Decorator.Custom.IsValid() )
		{
			bResult = Decorator.Custom->Evaluate( Context );
			if ( Decorator.bNot )
			{
				bResult = !bResult;
			}
		}
	}
	Decorator.bObserved = true;
	Decorator.bLastCondition = bResult;
	return bResult;
}

bool CBehaviorTree::NeedsReevaluation( const FCompiledDecorator& Decorator, FAIContext& Context ) const
{
	if ( !Decorator.bObserved )
	{
		return true;
	}
	if ( Decorator.Type == EBTDecoratorType::BlackboardCondition )
	{
		return Context.Blackboard != nullptr && Context.Blackboard->GetRevision( Decorator.Key ) != Decorator.ObservedRevision;
	}
	// 登録した判定は、何が変わると結果が変わるか分からないので、毎回評価し直す
	return true;
}

bool CBehaviorTree::CanEnter( int32 NodeIndex, FAIContext& Context )
{
	bool bPass = true;
	for ( FCompiledDecorator& Decorator : Nodes[NodeIndex].Decorators )
	{
		if ( IsConditionDecorator( Decorator.Type ) )
		{
			if ( !EvaluateCondition( Decorator, Context ) )
			{
				bPass = false;
			}
		}
		else if ( Decorator.Type == EBTDecoratorType::Cooldown && Context.Time < Decorator.CooldownEnd )
		{
			bPass = false;
		}
	}
	return bPass;
}

bool CBehaviorTree::CheckSelfAbort( int32 NodeIndex, FAIContext& Context )
{
	for ( FCompiledDecorator& Decorator : Nodes[NodeIndex].Decorators )
	{
		if ( IsConditionDecorator( Decorator.Type ) && AbortsSelf( Decorator.Abort ) && NeedsReevaluation( Decorator, Context ) && !EvaluateCondition( Decorator, Context ) )
		{
			return true;
		}
	}
	return false;
}

int32 CBehaviorTree::FindLowerPriorityTarget( int32 NodeIndex, FAIContext& Context )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	for ( int32 Position = 0; Position < Node.Cursor && Position < Node.Children.Num(); ++Position )
	{
		const int32 Child = Node.Children[Position];
		bool bBecameTrue = false;
		for ( FCompiledDecorator& Decorator : Nodes[Child].Decorators )
		{
			if ( IsConditionDecorator( Decorator.Type ) && AbortsLowerPriority( Decorator.Abort ) && NeedsReevaluation( Decorator, Context ) )
			{
				const bool bBefore = Decorator.bLastCondition;
				if ( EvaluateCondition( Decorator, Context ) && !bBefore )
				{
					bBecameTrue = true;
				}
			}
		}
		if ( bBecameTrue && CanEnter( Child, Context ) )
		{
			return Position;
		}
	}
	return -1;
}

float32 CBehaviorTree::GetTimeLimit( int32 NodeIndex ) const
{
	float32 Limit = 0.0f;
	for ( const FCompiledDecorator& Decorator : Nodes[NodeIndex].Decorators )
	{
		if ( Decorator.Type == EBTDecoratorType::TimeLimit && Decorator.Seconds > 0.0f && ( Limit <= 0.0f || Decorator.Seconds < Limit ) )
		{
			Limit = Decorator.Seconds;
		}
	}
	return Limit;
}

//=============================================================================
// 始まり・終わり・中断
//=============================================================================
void CBehaviorTree::Activate( int32 NodeIndex, FAIContext& Context )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	Node.bActive = true;
	Node.bTaskEntered = false;
	Node.Cursor = 0;
	Node.ActiveTime = 0.0f;
	for ( uint8& Result : Node.ParallelResults )
	{
		Result = 0;
	}
	for ( FCompiledDecorator& Decorator : Node.Decorators )
	{
		Decorator.RepeatCounter = 0;
	}
	for ( FCompiledService& Service : Node.Services )
	{
		Service.Part->OnActivate( Context );
		Service.Elapsed = Service.Interval;   // 最初の 1 回は、動き始めてすぐ
	}
}

void CBehaviorTree::Deactivate( int32 NodeIndex, FAIContext& Context, bool bAborted, EBTStatus Result )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	for ( FCompiledService& Service : Node.Services )
	{
		Service.Part->OnDeactivate( Context );
	}
	for ( FCompiledDecorator& Decorator : Node.Decorators )
	{
		if ( Decorator.Type == EBTDecoratorType::Cooldown )
		{
			Decorator.CooldownEnd = Context.Time + static_cast<float64>( Decorator.Seconds );
		}
	}
	Node.bActive = false;
	Node.bTaskEntered = false;
	Node.Cursor = 0;
	Node.ActiveTime = 0.0f;
	for ( uint8& ParallelResult : Node.ParallelResults )
	{
		ParallelResult = 0;
	}
	Node.LastResult = Result;
	Node.bHasResult = true;
	Node.bLastAborted = bAborted;
	Node.LastResultTime = Context.Time;
}

void CBehaviorTree::AbortNode( int32 NodeIndex, FAIContext& Context )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	if ( !Node.bActive )
	{
		return;
	}
	for ( int32 Position = Node.Children.Num() - 1; Position >= 0; --Position )
	{
		AbortNode( Node.Children[Position], Context );
	}
	if ( Node.Task.IsValid() && Node.bTaskEntered )
	{
		Node.Task->OnAbort( Context );
	}
	Deactivate( NodeIndex, Context, true, EBTStatus::Failure );
}

void CBehaviorTree::ResetBody( int32 NodeIndex )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	Node.Cursor = 0;
	Node.bTaskEntered = false;
	for ( uint8& Result : Node.ParallelResults )
	{
		Result = 0;
	}
}

void CBehaviorTree::Abort( FAIContext& Context )
{
	if ( !Nodes.IsEmpty() )
	{
		AbortNode( 0, Context );
	}
	RootResult = EBTStatus::Failure;
}

//=============================================================================
// 実行
//=============================================================================
void CBehaviorTree::TickServices( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	for ( FCompiledService& Service : Nodes[NodeIndex].Services )
	{
		Service.Elapsed += DeltaTime;
		if ( Service.Interval <= 0.0f || Service.Elapsed >= Service.Interval )
		{
			const float32 Elapsed = Service.Elapsed;
			Service.Elapsed = 0.0f;
			Service.Part->Tick( Context, Elapsed );
		}
	}
}

EBTStatus CBehaviorTree::ApplyResultDecorators( int32 NodeIndex, EBTStatus Status, bool& bOutRestart )
{
	TArray<FCompiledDecorator>& Decorators = Nodes[NodeIndex].Decorators;
	// 一番内側（最後）のデコレーターから外へ
	for ( int32 Index = Decorators.Num() - 1; Index >= 0; --Index )
	{
		FCompiledDecorator& Decorator = Decorators[Index];
		switch ( Decorator.Type )
		{
		case EBTDecoratorType::Inverter:
			Status = Status == EBTStatus::Success ? EBTStatus::Failure : EBTStatus::Success;
			break;
		case EBTDecoratorType::Succeeder:
			Status = EBTStatus::Success;
			break;
		case EBTDecoratorType::Failer:
			Status = EBTStatus::Failure;
			break;
		case EBTDecoratorType::Repeat:
		{
			++Decorator.RepeatCounter;
			bool bRestart = false;
			if ( Decorator.bUntilSuccess )
			{
				// 成功するまで繰り返す（Count があれば、その回数まで）
				bRestart = Status == EBTStatus::Failure && ( Decorator.Count == 0 || Decorator.RepeatCounter < Decorator.Count );
			}
			else if ( Status == EBTStatus::Success )
			{
				// 成功のあいだ繰り返す（Count があれば、その回数まで。無ければ無限）
				bRestart = Decorator.Count == 0 || Decorator.RepeatCounter < Decorator.Count;
			}
			if ( bRestart )
			{
				bOutRestart = true;
				return EBTStatus::Running;
			}
			break;
		}
		default:
			break;
		}
	}
	return Status;
}

EBTStatus CBehaviorTree::RunSelector( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	if ( Node.Cursor > 0 && Node.Cursor < Node.Children.Num() )
	{
		const int32 Target = FindLowerPriorityTarget( NodeIndex, Context );
		if ( Target >= 0 )
		{
			AbortNode( Node.Children[Node.Cursor], Context );
			Node.Cursor = Target;
		}
	}
	while ( Node.Cursor < Node.Children.Num() )
	{
		const EBTStatus Status = Run( Node.Children[Node.Cursor], Context, DeltaTime );
		if ( Status == EBTStatus::Running )
		{
			return EBTStatus::Running;
		}
		if ( Status == EBTStatus::Success )
		{
			return EBTStatus::Success;
		}
		++Node.Cursor;
	}
	return EBTStatus::Failure;
}

EBTStatus CBehaviorTree::RunSequence( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	while ( Node.Cursor < Node.Children.Num() )
	{
		const EBTStatus Status = Run( Node.Children[Node.Cursor], Context, DeltaTime );
		if ( Status == EBTStatus::Running )
		{
			return EBTStatus::Running;
		}
		if ( Status == EBTStatus::Failure )
		{
			return EBTStatus::Failure;
		}
		++Node.Cursor;
	}
	return EBTStatus::Success;
}

EBTStatus CBehaviorTree::RunParallel( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	const int32 NumChildren = Node.Children.Num();
	if ( NumChildren == 0 )
	{
		return Node.Mode == EBTParallelMode::AllSuccess ? EBTStatus::Success : EBTStatus::Failure;
	}

	// 終わっていない子を動かす（0 = 動いている、1 = 成功、2 = 失敗）
	for ( int32 Position = 0; Position < NumChildren; ++Position )
	{
		if ( Node.ParallelResults[Position] != 0 )
		{
			continue;
		}
		const EBTStatus Status = Run( Node.Children[Position], Context, DeltaTime );
		if ( Status == EBTStatus::Success )
		{
			Node.ParallelResults[Position] = 1;
		}
		else if ( Status == EBTStatus::Failure )
		{
			Node.ParallelResults[Position] = 2;
		}
	}

	int32 NumSuccess = 0;
	int32 NumFailure = 0;
	for ( int32 Position = 0; Position < NumChildren; ++Position )
	{
		NumSuccess += Node.ParallelResults[Position] == 1 ? 1 : 0;
		NumFailure += Node.ParallelResults[Position] == 2 ? 1 : 0;
	}

	EBTStatus Result = EBTStatus::Running;
	switch ( Node.Mode )
	{
	case EBTParallelMode::AllSuccess:
		if ( NumFailure > 0 )
		{
			Result = EBTStatus::Failure;
		}
		else if ( NumSuccess == NumChildren )
		{
			Result = EBTStatus::Success;
		}
		break;
	case EBTParallelMode::AnySuccess:
		if ( NumSuccess > 0 )
		{
			Result = EBTStatus::Success;
		}
		else if ( NumFailure == NumChildren )
		{
			Result = EBTStatus::Failure;
		}
		break;
	case EBTParallelMode::Primary:
		if ( Node.ParallelResults[0] != 0 )
		{
			Result = Node.ParallelResults[0] == 1 ? EBTStatus::Success : EBTStatus::Failure;
		}
		break;
	}
	if ( Result != EBTStatus::Running )
	{
		// 残っている子は中断する
		for ( int32 Position = NumChildren - 1; Position >= 0; --Position )
		{
			if ( Node.ParallelResults[Position] == 0 )
			{
				AbortNode( Node.Children[Position], Context );
			}
		}
	}
	return Result;
}

EBTStatus CBehaviorTree::RunBody( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	FCompiledNode& Node = Nodes[NodeIndex];
	switch ( Node.Type )
	{
	case EBTNodeType::Selector:
		return RunSelector( NodeIndex, Context, DeltaTime );
	case EBTNodeType::Sequence:
		return RunSequence( NodeIndex, Context, DeltaTime );
	case EBTNodeType::Parallel:
		return RunParallel( NodeIndex, Context, DeltaTime );
	case EBTNodeType::Task:
	{
		if ( !Node.Task.IsValid() )
		{
			return EBTStatus::Failure;
		}
		if ( !Node.bTaskEntered )
		{
			Node.bTaskEntered = true;
			Node.Task->OnEnter( Context );
		}
		const EBTStatus Status = Node.Task->OnTick( Context, DeltaTime );
		if ( Status != EBTStatus::Running )
		{
			Node.bTaskEntered = false;
		}
		return Status;
	}
	}
	return EBTStatus::Failure;
}

EBTStatus CBehaviorTree::Run( int32 NodeIndex, FAIContext& Context, float32 DeltaTime )
{
	if ( NumVisits >= MaxNodeVisitsPerTick )
	{
		if ( !bHitVisitLimit )
		{
			bHitVisitLimit = true;
			LOG( LogAI, Warning, "BehaviorTree: more than %d nodes were run in one tick (at '%s'). The rest of the tick was skipped.", MaxNodeVisitsPerTick, *Definition->GetNodeLabel( NodeIndex ) );
		}
		return EBTStatus::Running;
	}
	++NumVisits;

	FCompiledNode& Node = Nodes[NodeIndex];
	if ( !Node.bActive )
	{
		if ( !CanEnter( NodeIndex, Context ) )
		{
			Node.LastResult = EBTStatus::Failure;
			Node.bHasResult = true;
			Node.bLastAborted = false;
			Node.LastResultTime = Context.Time;
			return EBTStatus::Failure;
		}
		Activate( NodeIndex, Context );
	}
	else
	{
		Node.ActiveTime += DeltaTime;
		if ( CheckSelfAbort( NodeIndex, Context ) )
		{
			AbortNode( NodeIndex, Context );
			return EBTStatus::Failure;
		}
	}

	const float32 TimeLimit = GetTimeLimit( NodeIndex );
	if ( TimeLimit > 0.0f && Node.ActiveTime >= TimeLimit )
	{
		AbortNode( NodeIndex, Context );
		return EBTStatus::Failure;
	}

	TickServices( NodeIndex, Context, DeltaTime );
	EBTStatus Status = RunBody( NodeIndex, Context, DeltaTime );
	if ( Status != EBTStatus::Running )
	{
		bool bRestart = false;
		Status = ApplyResultDecorators( NodeIndex, Status, bRestart );
		if ( bRestart )
		{
			ResetBody( NodeIndex );
			return EBTStatus::Running;
		}
	}
	if ( Status != EBTStatus::Running )
	{
		Deactivate( NodeIndex, Context, false, Status );
	}
	return Status;
}

EBTStatus CBehaviorTree::Tick( FAIContext& Context, float32 DeltaTime )
{
	NumVisits = 0;
	if ( Nodes.IsEmpty() )
	{
		return EBTStatus::Failure;
	}
	RootResult = Run( 0, Context, DeltaTime );
	return RootResult;
}

//=============================================================================
// 状態を調べる
//=============================================================================
bool CBehaviorTree::IsNodeActive( int32 Node ) const
{
	return Node >= 0 && Node < Nodes.Num() && Nodes[Node].bActive;
}

EBTStatus CBehaviorTree::GetNodeResult( int32 Node ) const
{
	return Node >= 0 && Node < Nodes.Num() ? Nodes[Node].LastResult : EBTStatus::Failure;
}

bool CBehaviorTree::HasNodeResult( int32 Node ) const
{
	return Node >= 0 && Node < Nodes.Num() && Nodes[Node].bHasResult;
}

bool CBehaviorTree::WasNodeAborted( int32 Node ) const
{
	return Node >= 0 && Node < Nodes.Num() && Nodes[Node].bLastAborted;
}

float64 CBehaviorTree::GetNodeResultTime( int32 Node ) const
{
	return Node >= 0 && Node < Nodes.Num() ? Nodes[Node].LastResultTime : 0.0;
}

void CBehaviorTree::GetActiveNodes( TArray<int32>& OutNodes ) const
{
	OutNodes.Empty();
	for ( int32 Index = 0; Index < Nodes.Num(); ++Index )
	{
		if ( Nodes[Index].bActive )
		{
			OutNodes.Add( Index );
		}
	}
}

bool CBehaviorTree::GetDecoratorResult( int32 Node, int32 Decorator ) const
{
	return Node >= 0 && Node < Nodes.Num() && Decorator >= 0 && Decorator < Nodes[Node].Decorators.Num() && Nodes[Node].Decorators[Decorator].bLastCondition;
}
