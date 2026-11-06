// SPDX-License-Identifier: MIT
#include "AIGraphLayout.h"

#include "Library/Math/MathUtils.h"

namespace
{
	ImVec2 Add( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x + B.x, A.y + B.y );
	}

	// 箱の辺の真ん中（0 = 左、1 = 右、2 = 上、3 = 下）
	ImVec2 Anchor( const ImVec2& Min, const ImVec2& Size, int32 Side )
	{
		switch ( Side )
		{
		case 0:
			return ImVec2( Min.x, Min.y + Size.y * 0.5f );
		case 1:
			return ImVec2( Min.x + Size.x, Min.y + Size.y * 0.5f );
		case 2:
			return ImVec2( Min.x + Size.x * 0.5f, Min.y );
		default:
			return ImVec2( Min.x + Size.x * 0.5f, Min.y + Size.y );
		}
	}

	//=========================================================================
	// ステートマシン
	//=========================================================================
	struct FWorking
	{
		const FStateMachineDefinition* Definition = nullptr;
		const TArray<FStateTransitionInfo>* Transitions = nullptr;
		FStateMachineGraphLayout* Layout = nullptr;
		// 子ごとの列の番号（定義のステートの番号ごと）
		TArray<int32> Layer;
	};

	// マシンごとの列の幅と高さ（親のステートの番号 + 1 ごと）
	struct FColumns
	{
		TArray<float32> Width;
		TArray<float32> Height;
	};

	ImVec2 MeasureMachine( FWorking& Work, TArray<FColumns>& Columns, int32 Parent )
	{
		const FStateMachineDefinition& Definition = *Work.Definition;
		FStateGraphMachine& Machine = Work.Layout->Machines[Parent + 1];
		FColumns& Cols = Columns[Parent + 1];
		Machine.Children = Definition.GetChildren( Parent );
		const int32 NumChildren = Machine.Children.Num();

		// 入口の子
		const FString& EntryName = Parent < 0 ? Definition.Entry : Definition.States[Parent].Entry;
		int32 EntryChild = EntryName.IsEmpty() ? -1 : NAIGraph::TopChild( Definition, Parent, Definition.FindByPath( Parent, EntryName ) );
		if ( EntryChild < 0 && NumChildren > 0 )
		{
			EntryChild = Machine.Children[0];
		}
		Machine.EntryChild = EntryChild;

		// 入口から遷移をたどった順（幅優先）に、列の番号を振る
		for ( const int32 Child : Machine.Children )
		{
			Work.Layer[Child] = -1;
		}
		TArray<int32> Queue;
		int32 MaxLayer = 0;
		if ( EntryChild >= 0 )
		{
			Work.Layer[EntryChild] = 0;
			Queue.Add( EntryChild );
		}
		for ( int32 Head = 0; Head < Queue.Num(); ++Head )
		{
			const int32 From = Queue[Head];
			for ( const FStateTransitionInfo& Info : *Work.Transitions )
			{
				if ( Info.Machine != Parent || Info.bAnyTransition || Info.FromChild != From )
				{
					continue;
				}
				const int32 To = NAIGraph::TopChild( Definition, Parent, Info.Target );
				if ( To >= 0 && Work.Layer[To] < 0 )
				{
					Work.Layer[To] = Work.Layer[From] + 1;
					MaxLayer = NMath::Max( MaxLayer, Work.Layer[To] );
					Queue.Add( To );
				}
			}
		}
		// 入口からたどれなかったもの（Any State 遷移の先など）は、最後の列の次へ
		bool bUnreached = false;
		for ( const int32 Child : Machine.Children )
		{
			bUnreached = bUnreached || Work.Layer[Child] < 0;
		}
		if ( bUnreached )
		{
			++MaxLayer;
			for ( const int32 Child : Machine.Children )
			{
				if ( Work.Layer[Child] < 0 )
				{
					Work.Layer[Child] = MaxLayer;
				}
			}
		}

		// 子の大きさ（入れ子のマシンは、中身の大きさに枠を足す）
		for ( const int32 Child : Machine.Children )
		{
			if ( Definition.States[Child].bMachine )
			{
				const ImVec2 Inner = MeasureMachine( Work, Columns, Child );
				Work.Layout->Size[Child] = ImVec2( NMath::Max( Inner.x + NAIGraph::Padding * 2.0f, NAIGraph::LeafWidth ), Inner.y + NAIGraph::HeaderHeight + NAIGraph::Padding * 2.0f );
			}
			else
			{
				Work.Layout->Size[Child] = ImVec2( NAIGraph::LeafWidth, NAIGraph::LeafHeight );
			}
		}
		Machine.bHasAny = false;
		for ( const FStateTransitionInfo& Info : *Work.Transitions )
		{
			Machine.bHasAny = Machine.bHasAny || ( Info.Machine == Parent && Info.bAnyTransition );
		}

		// 列ごとの大きさ
		const int32 NumLayers = NumChildren > 0 ? MaxLayer + 1 : 0;
		Cols.Width.SetNum( NumLayers );
		Cols.Height.SetNum( NumLayers );
		for ( int32 Layer = 0; Layer < NumLayers; ++Layer )
		{
			float32 Width = 0.0f;
			float32 Height = 0.0f;
			int32 Count = 0;
			for ( const int32 Child : Machine.Children )
			{
				if ( Work.Layer[Child] == Layer )
				{
					Width = NMath::Max( Width, Work.Layout->Size[Child].x );
					Height += Work.Layout->Size[Child].y + ( Count > 0 ? NAIGraph::RowGap : 0.0f );
					++Count;
				}
			}
			Cols.Width[Layer] = Width;
			Cols.Height[Layer] = Height;
		}
		float32 ContentWidth = Machine.bHasAny ? NAIGraph::AnyWidth + NAIGraph::ColumnGap : 0.0f;
		float32 ContentHeight = Machine.bHasAny ? NAIGraph::AnyHeight : 0.0f;
		for ( int32 Layer = 0; Layer < NumLayers; ++Layer )
		{
			ContentWidth += Cols.Width[Layer] + ( Layer > 0 ? NAIGraph::ColumnGap : 0.0f );
			ContentHeight = NMath::Max( ContentHeight, Cols.Height[Layer] );
		}
		// 上下に、矢印を通す通路を足す
		Machine.Content = ImVec2( ContentWidth, ContentHeight + NAIGraph::Lane * 2.0f );
		return Machine.Content;
	}

	void PlaceMachine( FWorking& Work, const TArray<FColumns>& Columns, int32 Parent, const ImVec2& Origin )
	{
		FStateGraphMachine& Machine = Work.Layout->Machines[Parent + 1];
		const FColumns& Cols = Columns[Parent + 1];
		Machine.Origin = Origin;
		float32 X = Origin.x;
		if ( Machine.bHasAny )
		{
			Machine.AnyPos = ImVec2( Origin.x, Origin.y + ( Machine.Content.y - NAIGraph::AnyHeight ) * 0.5f );
			X += NAIGraph::AnyWidth + NAIGraph::ColumnGap;
		}
		for ( int32 Layer = 0; Layer < Cols.Width.Num(); ++Layer )
		{
			float32 Y = Origin.y + ( Machine.Content.y - Cols.Height[Layer] ) * 0.5f;
			for ( const int32 Child : Machine.Children )
			{
				if ( Work.Layer[Child] != Layer )
				{
					continue;
				}
				Work.Layout->Pos[Child] = ImVec2( X + ( Cols.Width[Layer] - Work.Layout->Size[Child].x ) * 0.5f, Y );
				Y += Work.Layout->Size[Child].y + NAIGraph::RowGap;
			}
			X += Cols.Width[Layer] + NAIGraph::ColumnGap;
		}
		// エディタで動かしたステートは、定義の Editor 欄の座標（このマシンの中身の左上からの相対）にする
		for ( const int32 Child : Machine.Children )
		{
			const NlohmannJson& Editor = Work.Definition->States[Child].Editor;
			if ( Editor.is_object() && Editor.contains( "X" ) && Editor.contains( "Y" ) && Editor["X"].is_number() && Editor["Y"].is_number() )
			{
				Work.Layout->Pos[Child] = ImVec2( Origin.x + Editor["X"].get<float>(), Origin.y + Editor["Y"].get<float>() );
			}
		}
		for ( const int32 Child : Machine.Children )
		{
			if ( Work.Definition->States[Child].bMachine )
			{
				PlaceMachine( Work, Columns, Child, Add( Work.Layout->Pos[Child], ImVec2( NAIGraph::Padding, NAIGraph::HeaderHeight + NAIGraph::Padding ) ) );
			}
		}
	}

	void AddRelay( FStateMachineGraphLayout& Layout, const ImVec2& Pos, int32 EdgeId )
	{
		for ( FStateGraphRelay& Relay : Layout.Relays )
		{
			if ( NMath::Abs( Relay.Pos.x - Pos.x ) < 1.0f && NMath::Abs( Relay.Pos.y - Pos.y ) < 1.0f )
			{
				if ( !Relay.Ids.Contains( EdgeId ) )
				{
					Relay.Ids.Add( EdgeId );
				}
				return;
			}
		}
		FStateGraphRelay Relay;
		Relay.Pos = Pos;
		Relay.Ids.Add( EdgeId );
		Layout.Relays.Add( Relay );
	}

	// 中継ノードが、持ち主ではない矢印・ほかの中継ノードに、これより近いと「重なっている」とみなす距離
	constexpr float RelayClearance = 10.0f;
	constexpr float RelaySpacing = 14.0f;

	float DistanceToSegment( const ImVec2& P, const ImVec2& A, const ImVec2& B )
	{
		const float Dx = B.x - A.x;
		const float Dy = B.y - A.y;
		const float LengthSq = Dx * Dx + Dy * Dy;
		float T = LengthSq > 0.0f ? ( ( P.x - A.x ) * Dx + ( P.y - A.y ) * Dy ) / LengthSq : 0.0f;
		T = NMath::Clamp( T, 0.0f, 1.0f );
		const float X = A.x + Dx * T - P.x;
		const float Y = A.y + Dy * T - P.y;
		return NMath::Sqrt( X * X + Y * Y );
	}

	float DistanceToRoute( const ImVec2& P, const FStateGraphRoute& Route )
	{
		float Best = 1.0e9f;
		for ( int32 Index = 1; Index < Route.Points.Num(); ++Index )
		{
			Best = NMath::Min( Best, DistanceToSegment( P, Route.Points[Index - 1], Route.Points[Index] ) );
		}
		return Best;
	}

	// Pos に置いた中継ノード（Self 番目。Ids の矢印のもの）が、ほかの矢印・中継ノードと重なるか
	bool IsRelayBlocked( const FStateMachineGraphLayout& Layout, const ImVec2& Pos, const TArray<int32>& Ids, int32 Self )
	{
		for ( const FStateGraphRoute& Route : Layout.Routes )
		{
			if ( !Ids.Contains( Route.Id ) && DistanceToRoute( Pos, Route ) < RelayClearance )
			{
				return true;
			}
		}
		for ( int32 Index = 0; Index < Layout.Relays.Num(); ++Index )
		{
			const float Dx = Layout.Relays[Index].Pos.x - Pos.x;
			const float Dy = Layout.Relays[Index].Pos.y - Pos.y;
			if ( Index != Self && Dx * Dx + Dy * Dy < RelaySpacing * RelaySpacing )
			{
				return true;
			}
		}
		return false;
	}

	// 関係のない矢印と重なっている中継ノードを、自分の矢印の直線部分に沿って、重ならない最寄りの位置へずらす
	void SeparateRelays( FStateMachineGraphLayout& Layout )
	{
		for ( int32 Index = 0; Index < Layout.Relays.Num(); ++Index )
		{
			const ImVec2 Old = Layout.Relays[Index].Pos;
			const TArray<int32> Ids = Layout.Relays[Index].Ids;
			if ( !IsRelayBlocked( Layout, Old, Ids, Index ) )
			{
				continue;
			}

			// 動かせる向き: 最初の矢印の、Old を通る線の向き（角にあるときは、前後の 2 つの線）
			TArray<ImVec2> Directions;
			for ( const FStateGraphRoute& Route : Layout.Routes )
			{
				if ( Route.Id != Ids[0] )
				{
					continue;
				}
				for ( int32 Segment = 1; Segment < Route.Points.Num(); ++Segment )
				{
					const ImVec2 A = Route.Points[Segment - 1];
					const ImVec2 B = Route.Points[Segment];
					const float SegmentLength = NMath::Sqrt( ( B.x - A.x ) * ( B.x - A.x ) + ( B.y - A.y ) * ( B.y - A.y ) );
					if ( SegmentLength > 0.5f && DistanceToSegment( Old, A, B ) < 0.5f )
					{
						Directions.Add( ImVec2( ( B.x - A.x ) / SegmentLength, ( B.y - A.y ) / SegmentLength ) );
						Directions.Add( ImVec2( ( A.x - B.x ) / SegmentLength, ( A.y - B.y ) / SegmentLength ) );
					}
				}
			}

			// 近い位置から順に試す。通る矢印すべての上に残る位置だけを使う
			bool bMoved = false;
			for ( float Distance = 6.0f; Distance <= 240.0f && !bMoved; Distance += 6.0f )
			{
				for ( const ImVec2& Direction : Directions )
				{
					const ImVec2 Candidate( Old.x + Direction.x * Distance, Old.y + Direction.y * Distance );
					bool bOnAll = true;
					for ( const FStateGraphRoute& Route : Layout.Routes )
					{
						bOnAll = bOnAll && ( !Ids.Contains( Route.Id ) || DistanceToRoute( Candidate, Route ) < 0.5f );
					}
					if ( bOnAll && !IsRelayBlocked( Layout, Candidate, Ids, Index ) )
					{
						Layout.Relays[Index].Pos = Candidate;
						bMoved = true;
						break;
					}
				}
			}

			// ラベルが中継ノードの位置にあった矢印は、ラベルも一緒に動かす
			if ( bMoved )
			{
				for ( FStateGraphRoute& Route : Layout.Routes )
				{
					if ( Ids.Contains( Route.Id ) && NMath::Abs( Route.Label.x - Old.x ) < 0.5f && NMath::Abs( Route.Label.y - Old.y ) < 0.5f )
					{
						Route.Label = Layout.Relays[Index].Pos;
					}
				}
			}
		}
	}

	// 矢印どうしが並んで重なる、とみなす距離（線の間隔）と、重なりの長さ
	constexpr float RouteSeparation = 8.0f;
	constexpr float RouteMinOverlap = 2.0f;
	// 重ならない位置を探すときの、ずらす幅
	constexpr float RouteNudgeStep = 10.0f;

	// 折れ線 Points（SkipIndex 番目の矢印の候補）が、ほかの矢印の線と並んで重なるか
	bool PathOverlapsOthers( const FStateMachineGraphLayout& Layout, int32 SkipIndex, const TArray<ImVec2>& Points )
	{
		for ( int32 Other = 0; Other < Layout.Routes.Num(); ++Other )
		{
			if ( Other == SkipIndex )
			{
				continue;
			}
			const FStateGraphRoute& Route = Layout.Routes[Other];
			for ( int32 Mine = 1; Mine < Points.Num(); ++Mine )
			{
				const ImVec2 A0 = Points[Mine - 1];
				const ImVec2 A1 = Points[Mine];
				const bool bMineVertical = NMath::Abs( A0.x - A1.x ) < 0.5f && NMath::Abs( A0.y - A1.y ) >= 0.5f;
				const bool bMineHorizontal = NMath::Abs( A0.y - A1.y ) < 0.5f && NMath::Abs( A0.x - A1.x ) >= 0.5f;
				if ( !bMineVertical && !bMineHorizontal )
				{
					continue;
				}
				for ( int32 Segment = 1; Segment < Route.Points.Num(); ++Segment )
				{
					const ImVec2 B0 = Route.Points[Segment - 1];
					const ImVec2 B1 = Route.Points[Segment];
					const bool bVertical = NMath::Abs( B0.x - B1.x ) < 0.5f && NMath::Abs( B0.y - B1.y ) >= 0.5f;
					const bool bHorizontal = NMath::Abs( B0.y - B1.y ) < 0.5f && NMath::Abs( B0.x - B1.x ) >= 0.5f;
					if ( ( bMineVertical && !bVertical ) || ( bMineHorizontal && !bHorizontal ) )
					{
						continue;
					}
					const float CoordinateGap = bMineVertical ? NMath::Abs( A0.x - B0.x ) : NMath::Abs( A0.y - B0.y );
					if ( CoordinateGap >= RouteSeparation )
					{
						continue;
					}
					const float MineLow = bMineVertical ? NMath::Min( A0.y, A1.y ) : NMath::Min( A0.x, A1.x );
					const float MineHigh = bMineVertical ? NMath::Max( A0.y, A1.y ) : NMath::Max( A0.x, A1.x );
					const float OtherLow = bMineVertical ? NMath::Min( B0.y, B1.y ) : NMath::Min( B0.x, B1.x );
					const float OtherHigh = bMineVertical ? NMath::Max( B0.y, B1.y ) : NMath::Max( B0.x, B1.x );
					if ( NMath::Min( MineHigh, OtherHigh ) - NMath::Max( MineLow, OtherLow ) > RouteMinOverlap )
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	// 他の矢印と並んで重なる矢印を、避けて置き直す。真ん中の線のずらし（MiddleMin..MiddleMax）と、箱に付いたままの平行移動（ShiftLimit）を、
	// 動きの小さい順に組み合わせて試す（入れ違いの 2 本のように、真ん中の線だけではどうしても重なる配置もある）
	void NudgeRoutes( FStateMachineGraphLayout& Layout )
	{
		for ( int32 Index = 0; Index < Layout.Routes.Num(); ++Index )
		{
			FStateGraphRoute& Route = Layout.Routes[Index];
			const bool bCanNudgeMiddle = Route.MiddleMax > Route.MiddleMin;
			const bool bCanShift = Route.ShiftLimit > 0.0f;
			if ( ( !bCanNudgeMiddle && !bCanShift ) || Route.Points.Num() != 4 || !PathOverlapsOthers( Layout, Index, Route.Points ) )
			{
				continue;
			}
			// 最初の線が横なら、経路全体は上下へ動かし、真ん中の線（縦）は左右へ動かす。縦なら、その逆
			const bool bFirstHorizontal = NMath::Abs( Route.Points[1].y - Route.Points[0].y ) < 0.5f;
			const bool bFirstVertical = NMath::Abs( Route.Points[1].x - Route.Points[0].x ) < 0.5f;
			if ( bFirstHorizontal == bFirstVertical )
			{
				continue;
			}
			const float MiddleCurrent = bFirstHorizontal ? Route.Points[1].x : Route.Points[1].y;

			bool bFound = false;
			for ( int32 ShiftStep = 0; ShiftStep <= 8 && !bFound; ++ShiftStep )
			{
				for ( int32 ShiftSide = 0; ShiftSide < ( ShiftStep == 0 ? 1 : 2 ) && !bFound; ++ShiftSide )
				{
					const float Shift = ( ShiftSide == 0 ? 1.0f : -1.0f ) * RouteNudgeStep * static_cast<float>( ShiftStep );
					if ( NMath::Abs( Shift ) > Route.ShiftLimit )
					{
						continue;
					}
					for ( int32 MiddleStep = 0; MiddleStep <= 16 && !bFound; ++MiddleStep )
					{
						for ( int32 MiddleSide = 0; MiddleSide < ( MiddleStep == 0 ? 1 : 2 ) && !bFound; ++MiddleSide )
						{
							const float Middle = MiddleCurrent + ( MiddleSide == 0 ? 1.0f : -1.0f ) * RouteNudgeStep * static_cast<float>( MiddleStep );
							if ( MiddleStep > 0 && ( !bCanNudgeMiddle || Middle < Route.MiddleMin || Middle > Route.MiddleMax ) )
							{
								continue;
							}
							TArray<ImVec2> Candidate = Route.Points;
							for ( ImVec2& Point : Candidate )
							{
								( bFirstHorizontal ? Point.y : Point.x ) += Shift;
							}
							( bFirstHorizontal ? Candidate[1].x : Candidate[1].y ) = Middle;
							( bFirstHorizontal ? Candidate[2].x : Candidate[2].y ) = Middle;
							if ( !PathOverlapsOthers( Layout, Index, Candidate ) )
							{
								Route.Points = Candidate;
								bFound = true;
							}
						}
					}
				}
			}
		}
	}

	// 経路が決まったあとで、ラベルと中継ノードを置く
	void PlaceLabelsAndRelays( FStateMachineGraphLayout& Layout )
	{
		for ( FStateGraphRoute& Route : Layout.Routes )
		{
			const ImVec2& A = Route.bLabelOnLast ? Route.Points[2] : Route.Points[1];
			const ImVec2& B = Route.bLabelOnLast ? Route.Points[3] : Route.Points[2];
			Route.Label = ImVec2( ( A.x + B.x ) * 0.5f, ( A.y + B.y ) * 0.5f );
			if ( Route.RelayAt == 0 )
			{
				AddRelay( Layout, ImVec2( ( Route.Points[1].x + Route.Points[2].x ) * 0.5f, ( Route.Points[1].y + Route.Points[2].y ) * 0.5f ), Route.Id );
			}
			else if ( Route.RelayAt == 1 || Route.RelayAt == 2 )
			{
				AddRelay( Layout, Route.Points[Route.RelayAt], Route.Id );
			}
		}
	}

	struct FEdgeInfo
	{
		int32 Id = -1;
		int32 Machine = -1;
		int32 From = -1;
		int32 To = -1;
		bool bAny = false;
		// 同じ 2 つの箱の間の矢印を、重ならないようにずらす量
		float32 Offset = 0.0f;
		// この矢印にまとめた遷移の Id（同じ元・同じ先・同じ種類の遷移）
		TArray<int32> Ids;
	};
}

int32 NAIGraph::TopChild( const FStateMachineDefinition& Definition, int32 Parent, int32 State )
{
	for ( int32 Depth = 0; State >= 0 && Depth < Definition.States.Num() + 1; ++Depth )
	{
		if ( Definition.States[State].Parent == Parent )
		{
			return State;
		}
		State = Definition.States[State].Parent;
	}
	return -1;
}

FStateMachineGraphLayout NAIGraph::BuildStateMachineLayout( const FStateMachineDefinition& Definition, const TArray<FStateTransitionInfo>& Transitions )
{
	FStateMachineGraphLayout Layout;
	Layout.Pos.SetNum( Definition.States.Num() );
	Layout.Size.SetNum( Definition.States.Num() );
	Layout.Machines.SetNum( Definition.States.Num() + 1 );
	if ( Definition.States.IsEmpty() )
	{
		return Layout;
	}

	FWorking Work;
	Work.Definition = &Definition;
	Work.Transitions = &Transitions;
	Work.Layout = &Layout;
	Work.Layer.SetNum( Definition.States.Num() );
	TArray<FColumns> Columns;
	Columns.SetNum( Definition.States.Num() + 1 );
	Layout.Content = MeasureMachine( Work, Columns, -1 );
	PlaceMachine( Work, Columns, -1, ImVec2( 0.0f, 0.0f ) );
	// 動かしたステートが、最初の範囲の外へ出たら、グラフ全体の大きさを広げる
	for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
	{
		Layout.Content.x = NMath::Max( Layout.Content.x, Layout.Pos[Index].x + Layout.Size[Index].x + NAIGraph::Padding );
		Layout.Content.y = NMath::Max( Layout.Content.y, Layout.Pos[Index].y + Layout.Size[Index].y + NAIGraph::Padding );
	}

	// 矢印の一覧
	TArray<FEdgeInfo> Edges;
	for ( const FStateTransitionInfo& Info : Transitions )
	{
		FEdgeInfo Edge;
		Edge.Id = Info.Id;
		Edge.Machine = Info.Machine;
		Edge.bAny = Info.bAnyTransition;
		Edge.From = Info.bAnyTransition ? -1 : Info.FromChild;
		Edge.To = TopChild( Definition, Info.Machine, Info.Target );
		Edge.Ids.Add( Info.Id );
		if ( Edge.To < 0 )
		{
			continue;
		}
		// 同じ元・同じ先・同じ種類の遷移は、何本あっても 1 本の矢印にまとめる（矢印が重なって読めなくなるのを防ぐ）
		bool bMerged = false;
		for ( FEdgeInfo& Existing : Edges )
		{
			if ( Existing.Machine == Edge.Machine && Existing.From == Edge.From && Existing.To == Edge.To && Existing.bAny == Edge.bAny )
			{
				Existing.Ids.Add( Info.Id );
				bMerged = true;
				break;
			}
		}
		if ( !bMerged )
		{
			Edges.Add( Edge );
		}
	}
	// 同じ 2 つの箱の間の矢印は、重ならないようにずらす。前向きの矢印の、分かれ目と合わさる所の数も数える
	TArray<int32> ForwardOut;
	TArray<int32> ForwardIn;
	ForwardOut.SetNum( Edges.Num() );
	ForwardIn.SetNum( Edges.Num() );
	for ( int32 Index = 0; Index < Edges.Num(); ++Index )
	{
		int32 Count = 0;
		int32 Order = 0;
		for ( int32 Other = 0; Other < Edges.Num(); ++Other )
		{
			const bool bSamePair = Edges[Other].Machine == Edges[Index].Machine && ( ( Edges[Other].From == Edges[Index].From && Edges[Other].To == Edges[Index].To ) || ( Edges[Other].From == Edges[Index].To && Edges[Other].To == Edges[Index].From ) );
			if ( bSamePair )
			{
				Order += Other < Index ? 1 : 0;
				++Count;
			}
			if ( Edges[Other].Machine == Edges[Index].Machine && !Edges[Other].bAny && Edges[Other].From != Edges[Other].To )
			{
				const ImVec2 OtherSourceMin = Layout.Pos[Edges[Other].From];
				const bool bForward = Layout.Pos[Edges[Other].To].x >= OtherSourceMin.x + Layout.Size[Edges[Other].From].x - 1.0f;
				if ( bForward && Edges[Other].From == Edges[Index].From )
				{
					++ForwardOut[Index];
				}
				if ( bForward && Edges[Other].To == Edges[Index].To )
				{
					++ForwardIn[Index];
				}
			}
		}
		Edges[Index].Offset = ( static_cast<float32>( Order ) - static_cast<float32>( Count - 1 ) * 0.5f ) * 13.0f;
	}

	TArray<int32> AnyLane;
	TArray<int32> BackLane;
	AnyLane.SetNum( Definition.States.Num() + 1 );
	BackLane.SetNum( Definition.States.Num() + 1 );
	for ( int32 Index = 0; Index < Edges.Num(); ++Index )
	{
		const FEdgeInfo& Edge = Edges[Index];
		const FStateGraphMachine& Machine = Layout.Machines[Edge.Machine + 1];
		const ImVec2 SourceMin = Edge.From >= 0 ? Layout.Pos[Edge.From] : Machine.AnyPos;
		const ImVec2 SourceSize = Edge.From >= 0 ? Layout.Size[Edge.From] : ImVec2( NAIGraph::AnyWidth, NAIGraph::AnyHeight );
		const ImVec2 TargetMin = Layout.Pos[Edge.To];
		const ImVec2 TargetSize = Layout.Size[Edge.To];

		FStateGraphRoute Route;
		Route.Id = Edge.Id;
		Route.Machine = Edge.Machine;
		Route.From = Edge.From;
		Route.To = Edge.To;
		Route.bAny = Edge.bAny;
		Route.Ids = Edge.Ids;
		TArray<ImVec2>& Points = Route.Points;

		if ( Edge.bAny )
		{
			// 箱の上の通路を回る（中継ノードが、その真ん中）
			const int32 Lane = AnyLane[Edge.Machine + 1]++;
			const float32 TopY = Machine.Origin.y + 12.0f + static_cast<float32>( Lane ) * NAIGraph::LaneStep;
			const ImVec2 P0 = Add( Anchor( SourceMin, SourceSize, 2 ), ImVec2( static_cast<float32>( Lane ) * 7.0f, 0.0f ) );
			const ImVec2 P1 = Add( Anchor( TargetMin, TargetSize, 2 ), ImVec2( Edge.Offset, 0.0f ) );
			Points.Add( P0 );
			Points.Add( ImVec2( P0.x, TopY ) );
			Points.Add( ImVec2( P1.x, TopY ) );
			Points.Add( P1 );
			Route.RelayAt = 0;
			Route.MiddleMin = Machine.Origin.y + 4.0f;
			Route.MiddleMax = NMath::Max( Machine.Origin.y + NAIGraph::Lane - 14.0f, TopY );
			Route.ShiftLimit = NMath::Max( NMath::Min( SourceSize.x, TargetSize.x ) * 0.5f - 12.0f, 0.0f );
		}
		else if ( Edge.From == Edge.To )
		{
			// 自分へ戻る: 箱の上で小さく回る
			const ImVec2 Top = Anchor( SourceMin, SourceSize, 2 );
			const ImVec2 A( Top.x + SourceSize.x * 0.22f + Edge.Offset, Top.y );
			const ImVec2 B( Top.x - SourceSize.x * 0.22f + Edge.Offset, Top.y );
			Points.Add( A );
			Points.Add( ImVec2( A.x, Top.y - 20.0f ) );
			Points.Add( ImVec2( B.x, Top.y - 20.0f ) );
			Points.Add( B );
			Route.Label = ImVec2( Top.x + Edge.Offset, Top.y - 20.0f );
		}
		else if ( TargetMin.x >= SourceMin.x + SourceSize.x - 1.0f )
		{
			// 前（右）へ: 横 -> 縦 -> 横。分かれ目（または合わさる所）に中継ノードを置く
			const ImVec2 P0 = Add( Anchor( SourceMin, SourceSize, 1 ), ImVec2( 0.0f, Edge.Offset ) );
			const ImVec2 P1 = Add( Anchor( TargetMin, TargetSize, 0 ), ImVec2( 0.0f, Edge.Offset ) );
			const bool bFanOut = ForwardOut[Index] >= 2;
			const bool bFanIn = ForwardIn[Index] >= 2;
			float32 MidX = P0.x + NMath::Min( 34.0f, ( P1.x - P0.x ) * 0.5f );
			if ( !bFanOut && bFanIn )
			{
				MidX = P1.x - NMath::Min( 34.0f, ( P1.x - P0.x ) * 0.5f );
			}
			Points.Add( P0 );
			Points.Add( ImVec2( MidX, P0.y ) );
			Points.Add( ImVec2( MidX, P1.y ) );
			Points.Add( P1 );
			Route.bLabelOnLast = true;
			if ( bFanOut )
			{
				Route.RelayAt = 1;
			}
			else if ( bFanIn )
			{
				Route.RelayAt = 2;
			}
			else if ( P1.x - P0.x > 24.0f )
			{
				// 分かれ目・合わさる所のない矢印だけ、縦の線を横へずらしてよい
				Route.MiddleMin = P0.x + 12.0f;
				Route.MiddleMax = P1.x - 12.0f;
			}
			if ( !bFanOut && !bFanIn )
			{
				Route.ShiftLimit = NMath::Max( NMath::Min( SourceSize.y, TargetSize.y ) * 0.5f - 8.0f, 0.0f );
			}
		}
		else if ( TargetMin.x + TargetSize.x <= SourceMin.x + 1.0f )
		{
			// 戻る（左）へ: 箱の下の通路を回る（中継ノードが、その真ん中）
			const int32 Lane = BackLane[Edge.Machine + 1]++;
			const float32 BottomY = Machine.Origin.y + Machine.Content.y - 12.0f - static_cast<float32>( Lane ) * NAIGraph::LaneStep;
			const float32 Shift = Edge.Offset + static_cast<float32>( Lane ) * 7.0f;
			const ImVec2 P0 = Add( Anchor( SourceMin, SourceSize, 3 ), ImVec2( Shift, 0.0f ) );
			const ImVec2 P1 = Add( Anchor( TargetMin, TargetSize, 3 ), ImVec2( Shift, 0.0f ) );
			Points.Add( P0 );
			Points.Add( ImVec2( P0.x, BottomY ) );
			Points.Add( ImVec2( P1.x, BottomY ) );
			Points.Add( P1 );
			Route.RelayAt = 0;
			Route.MiddleMin = NMath::Min( Machine.Origin.y + Machine.Content.y - NAIGraph::Lane + 14.0f, BottomY );
			Route.MiddleMax = Machine.Origin.y + Machine.Content.y - 4.0f;
			Route.ShiftLimit = NMath::Max( NMath::Min( SourceSize.x, TargetSize.x ) * 0.5f - 12.0f, 0.0f );
		}
		else
		{
			// 同じ列の上下へ: 縦 -> 横 -> 縦
			const bool bDown = TargetMin.y >= SourceMin.y;
			const ImVec2 P0 = Add( Anchor( SourceMin, SourceSize, bDown ? 3 : 2 ), ImVec2( Edge.Offset, 0.0f ) );
			const ImVec2 P1 = Add( Anchor( TargetMin, TargetSize, bDown ? 2 : 3 ), ImVec2( Edge.Offset, 0.0f ) );
			const float32 MidY = ( P0.y + P1.y ) * 0.5f;
			Points.Add( P0 );
			Points.Add( ImVec2( P0.x, MidY ) );
			Points.Add( ImVec2( P1.x, MidY ) );
			Points.Add( P1 );
			if ( NMath::Abs( P1.y - P0.y ) > 24.0f )
			{
				Route.MiddleMin = NMath::Min( P0.y, P1.y ) + 10.0f;
				Route.MiddleMax = NMath::Max( P0.y, P1.y ) - 10.0f;
			}
			Route.ShiftLimit = NMath::Max( NMath::Min( SourceSize.x, TargetSize.x ) * 0.5f - 12.0f, 0.0f );
		}
		Layout.Routes.Add( Route );
	}
	NudgeRoutes( Layout );
	PlaceLabelsAndRelays( Layout );
	SeparateRelays( Layout );
	return Layout;
}

//=============================================================================
// ビヘイビアツリー
//=============================================================================
namespace
{
	struct FTreeWorking
	{
		const FBehaviorTreeDefinition* Definition = nullptr;
		TArray<float32> CenterX;
		TArray<int32> Depth;
		float32 NextX = 0.0f;
	};

	void PlaceTreeNode( FTreeWorking& Work, int32 Node, int32 Depth )
	{
		Work.Depth[Node] = Depth;
		const TArray<int32> Children = Work.Definition->GetChildren( Node );
		if ( Children.IsEmpty() )
		{
			Work.CenterX[Node] = Work.NextX + NAIGraph::TreeNodeWidth * 0.5f;
			Work.NextX += NAIGraph::TreeNodeWidth + NAIGraph::TreeGapX;
			return;
		}
		for ( const int32 Child : Children )
		{
			PlaceTreeNode( Work, Child, Depth + 1 );
		}
		Work.CenterX[Node] = ( Work.CenterX[Children[0]] + Work.CenterX[Children.Last()] ) * 0.5f;
	}
}

FBehaviorTreeGraphLayout NAIGraph::BuildBehaviorTreeLayout( const FBehaviorTreeDefinition& Definition )
{
	FBehaviorTreeGraphLayout Layout;
	const int32 NumNodes = Definition.Nodes.Num();
	Layout.Pos.SetNum( NumNodes );
	Layout.Size.SetNum( NumNodes );
	Layout.Links.SetNum( NumNodes );
	if ( NumNodes == 0 )
	{
		return Layout;
	}

	FTreeWorking Work;
	Work.Definition = &Definition;
	Work.CenterX.SetNum( NumNodes );
	Work.Depth.SetNum( NumNodes );
	PlaceTreeNode( Work, 0, 0 );

	// 高さは、デコレーターとサービスの数で決まる。深さごとの、いちばん高い箱で段を決める
	int32 MaxDepth = 0;
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		const FBTNodeDef& Node = Definition.Nodes[Index];
		const bool bHasLines = !Node.Decorators.IsEmpty() || !Node.Services.IsEmpty();
		Layout.Size[Index] = ImVec2( NAIGraph::TreeNodeWidth, NAIGraph::TreeHeaderHeight + NAIGraph::TreeLineHeight * static_cast<float32>( Node.Decorators.Num() + Node.Services.Num() ) + ( bHasLines ? 6.0f : 0.0f ) );
		MaxDepth = NMath::Max( MaxDepth, Work.Depth[Index] );
	}
	TArray<float32> DepthHeight;
	TArray<float32> DepthY;
	DepthHeight.SetNum( MaxDepth + 1 );
	DepthY.SetNum( MaxDepth + 1 );
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		DepthHeight[Work.Depth[Index]] = NMath::Max( DepthHeight[Work.Depth[Index]], Layout.Size[Index].y );
	}
	float32 Y = 0.0f;
	for ( int32 Depth = 0; Depth <= MaxDepth; ++Depth )
	{
		DepthY[Depth] = Y;
		Y += DepthHeight[Depth] + NAIGraph::TreeGapY;
	}
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		Layout.Pos[Index] = ImVec2( Work.CenterX[Index] - NAIGraph::TreeNodeWidth * 0.5f, DepthY[Work.Depth[Index]] );
	}
	Layout.Content = ImVec2( NMath::Max( Work.NextX - NAIGraph::TreeGapX, NAIGraph::TreeNodeWidth ), NMath::Max( Y - NAIGraph::TreeGapY, 40.0f ) );

	// 親から子への線: 縦 -> 横 -> 縦（段の間の同じ高さで横に走るので、兄弟の線がそろう）。子が 2 つ以上なら、分かれ目に中継ノード
	for ( int32 Node = 0; Node < NumNodes; ++Node )
	{
		const int32 Parent = Definition.Nodes[Node].Parent;
		if ( Parent < 0 )
		{
			continue;
		}
		const ImVec2 P0( Work.CenterX[Parent], Layout.Pos[Parent].y + DepthHeight[Work.Depth[Parent]] );
		const ImVec2 P1( Work.CenterX[Node], Layout.Pos[Node].y );
		const float32 MidY = P0.y + NAIGraph::TreeGapY * 0.5f;
		TArray<ImVec2>& Link = Layout.Links[Node];
		Link.Add( ImVec2( P0.x, Layout.Pos[Parent].y + Layout.Size[Parent].y ) );
		Link.Add( ImVec2( P0.x, MidY ) );
		Link.Add( ImVec2( P1.x, MidY ) );
		Link.Add( P1 );
	}
	for ( int32 Node = 0; Node < NumNodes; ++Node )
	{
		if ( Definition.GetChildren( Node ).Num() >= 2 )
		{
			FBehaviorTreeGraphRelay Relay;
			Relay.Parent = Node;
			Relay.Pos = ImVec2( Work.CenterX[Node], Layout.Pos[Node].y + DepthHeight[Work.Depth[Node]] + NAIGraph::TreeGapY * 0.5f );
			Layout.Relays.Add( Relay );
		}
	}
	return Layout;
}
