// SPDX-License-Identifier: MIT
#include "MaterialEditor.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialCompiler.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	constexpr float NodeWidth = 180.0f;
	constexpr float HeaderHeight = 24.0f;
	constexpr float RowHeight = 20.0f;
	constexpr float PinHitRadius = 9.0f;

	int32 NumRows( const FMaterialNode& Node )
	{
		return NMath::Max( 1, NMaterialNodes::GetInfo( Node.Kind ).NumPins );
	}

	ImVec2 NodeSize( int32 Rows )
	{
		return ImVec2( NodeWidth, HeaderHeight + RowHeight * static_cast<float>( Rows ) + 6.0f );
	}

	ImVec2 InputPinPosition( const FMaterialNode& Node, int32 Pin )
	{
		return ImVec2( Node.Position.x, Node.Position.y + HeaderHeight + RowHeight * ( static_cast<float>( Pin ) + 0.5f ) );
	}

	ImVec2 OutputPinPosition( const FMaterialNode& Node )
	{
		return ImVec2( Node.Position.x + NodeWidth, Node.Position.y + HeaderHeight + RowHeight * 0.5f );
	}

	ImU32 CategoryColor( const char* Category )
	{
		const FString Name( Category );
		if ( Name == "Values" )
		{
			return IM_COL32( 62, 96, 160, 255 );
		}
		if ( Name == "Inputs" )
		{
			return IM_COL32( 54, 128, 86, 255 );
		}
		if ( Name == "Utility" )
		{
			return IM_COL32( 160, 106, 52, 255 );
		}
		if ( Name == "Substrate" )
		{
			return IM_COL32( 128, 74, 160, 255 );
		}
		return IM_COL32( 86, 88, 100, 255 );
	}

	// ノードの見出し: 種類の名前と、名前のあるもの（パラメーターなど）は、その名前
	FString NodeTitle( const FMaterialNode& Node )
	{
		FString Title = NMaterialNodes::GetInfo( Node.Kind ).Name;
		if ( !Node.Name.IsEmpty() )
		{
			Title += " : " + Node.Name;
		}
		return Title;
	}

	bool IsDiagnosed( const TArray<FMaterialDiagnostic>& Diagnostics, int32 NodeId, EMaterialSeverity Severity )
	{
		for ( int32 Index = 0; Index < Diagnostics.Num(); ++Index )
		{
			if ( Diagnostics[Index].NodeId == NodeId && Diagnostics[Index].Severity == Severity )
			{
				return true;
			}
		}
		return false;
	}

	void SetStatus( FMaterialEditorState& State, const FString& Message, bool bError )
	{
		State.StatusMessage = Message;
		State.bStatusIsError = bError;
	}

	bool IsReadOnlyPath( const FString& Path )
	{
		return Path.StartsWith( "Engine:" ) || Path.StartsWith( "engine:" ) || Path.StartsWith( "ENGINE:" );
	}

	void MarkChanged( FMaterialEditorState& State )
	{
		State.bDirty = true;
		NMaterialEditorUI::Recompile( State );
	}

	// 検索の文字（大文字小文字は区別しない）に合うか
	bool MatchesSearch( const char* Name, const char* Search )
	{
		return Search[0] == '\0' || FString( Name ).Contains( FString( Search ), ESearchCase::IgnoreCase );
	}

	// ノードの一覧（分類ごと）。選ばれたら、種類を返す
	bool DrawPalette( FMaterialEditorState& State, EMaterialNodeKind& OutKind )
	{
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::InputTextWithHint( "##search", "search nodes", State.SearchBuffer, sizeof( State.SearchBuffer ) );
		bool bPicked = false;
		const char* Categories[] = { "Values", "Inputs", "Math", "Utility", "Substrate" };
		for ( const char* Category : Categories )
		{
			bool bAny = false;
			for ( int32 Index = 0; Index < static_cast<int32>( EMaterialNodeKind::Count ) && !bAny; ++Index )
			{
				const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( static_cast<EMaterialNodeKind>( Index ) );
				bAny = FString( Info.Category ) == Category && NMaterialNodes::IsAvailable( Info.Kind, State.Definition.Domain ) && MatchesSearch( Info.Name, State.SearchBuffer );
			}
			if ( !bAny )
			{
				continue;
			}
			ImGui::PushStyleColor( ImGuiCol_Text, CategoryColor( Category ) );
			const bool bOpen = ImGui::TreeNodeEx( Category, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth );
			ImGui::PopStyleColor();
			if ( !bOpen )
			{
				continue;
			}
			for ( int32 Index = 0; Index < static_cast<int32>( EMaterialNodeKind::Count ); ++Index )
			{
				const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( static_cast<EMaterialNodeKind>( Index ) );
				if ( FString( Info.Category ) != Category || !NMaterialNodes::IsAvailable( Info.Kind, State.Definition.Domain ) || !MatchesSearch( Info.Name, State.SearchBuffer ) )
				{
					continue;
				}
				if ( ImGui::Selectable( Info.Name ) )
				{
					OutKind = Info.Kind;
					bPicked = true;
				}
			}
			ImGui::TreePop();
		}
		return bPicked;
	}

	// 1 つのノードの、最初の名前・値
	FString UniqueName( const FMaterialEditorState& State, const char* Base )
	{
		for ( int32 Number = 1; Number < 1000; ++Number )
		{
			const FString Candidate = FString::Printf( "%s%d", Base, Number );
			bool bUsed = false;
			for ( int32 Index = 0; Index < State.Definition.Graph.Nodes.Num(); ++Index )
			{
				bUsed = bUsed || State.Definition.Graph.Nodes[Index].Name == Candidate;
			}
			if ( !bUsed )
			{
				return Candidate;
			}
		}
		return FString( Base );
	}
}

// ---- 操作 ----------------------------------------------------------------------------------------
void NMaterialEditorUI::Recompile( FMaterialEditorState& State )
{
	if ( State.Material )
	{
		State.Material->Reload( State.Definition );
	}
	else
	{
		State.Material = CMaterial::Create( State.Definition );
	}
	State.Diagnostics = State.Material->GetDiagnostics();
}

void NMaterialEditorUI::NewMaterial( FMaterialEditorState& State, EMaterialDomain Domain, EMaterialShading Shading )
{
	State.Definition = FMaterialDefinition();
	State.Definition.Name = "NewMaterial";
	State.Definition.Domain = Domain;
	State.Definition.Shading = Shading;
	State.Material.Reset();
	State.FilePath.Empty();
	State.SelectedNode = -1;
	State.MovingNode = -1;
	State.DragFromNode = -1;
	State.View = FNodeGraphView();
	State.bDirty = false;
	Recompile( State );
	SetStatus( State, "New material", false );
}

bool NMaterialEditorUI::OpenMaterial( FMaterialEditorState& State, const FString& Path )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	const TOptional<FString> Resolved = Assets != nullptr ? Assets->ResolvePath( Path ) : TOptional<FString>();
	TArray<uint8> Bytes;
	if ( !Resolved.IsSet() || !NFile::ReadAllBytes( Resolved.GetValue(), Bytes ) )
	{
		SetStatus( State, "Cannot read '" + Path + "'", true );
		return false;
	}
	int32 Start = 0;
	if ( Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF )
	{
		Start = 3;
	}
	TArray<char> Chars;
	for ( int32 Index = Start; Index < Bytes.Num(); ++Index )
	{
		Chars.Add( static_cast<char>( Bytes[Index] ) );
	}
	Chars.Add( '\0' );
	FMaterialDefinition Loaded;
	FString Error;
	if ( !FMaterialDefinition::FromJson( FString( Chars.GetData() ), Loaded, &Error ) )
	{
		SetStatus( State, "'" + Path + "' is not a material: " + Error, true );
		return false;
	}
	// 位置が決まっていない（ノード同士が重なる）か、画面の外（負）にあるときは、並べ直す
	bool bStacked = false;
	bool bNegative = false;
	for ( int32 Index = 0; Index < Loaded.Graph.Nodes.Num(); ++Index )
	{
		const FMaterialNode& Node = Loaded.Graph.Nodes[Index];
		bNegative = bNegative || Node.Position.x < 0.0f || Node.Position.y < 0.0f;
		for ( int32 Other = 0; Other < Index; ++Other )
		{
			const FMaterialNode& Previous = Loaded.Graph.Nodes[Other];
			const bool bSeparatedX = Node.Position.x + NodeWidth <= Previous.Position.x || Previous.Position.x + NodeWidth <= Node.Position.x;
			const bool bSeparatedY = Node.Position.y + NodeSize( NumRows( Node ) ).y <= Previous.Position.y || Previous.Position.y + NodeSize( NumRows( Previous ) ).y <= Node.Position.y;
			bStacked = bStacked || ( !bSeparatedX && !bSeparatedY );
		}
	}
	State.Definition = Loaded;
	if ( bStacked || bNegative )
	{
		ArrangeNodes( State );
	}
	State.Material.Reset();
	State.FilePath = Path;
	State.SaveAsPath = Path;
	State.SelectedNode = -1;
	State.MovingNode = -1;
	State.DragFromNode = -1;
	State.View = FNodeGraphView();
	State.bDirty = false;
	State.View.bFitted = false;
	Recompile( State );
	SetStatus( State, "Opened " + Path, false );
	return true;
}

bool NMaterialEditorUI::SaveMaterial( FMaterialEditorState& State, const FString& InPath )
{
	const FString Path = InPath.IsEmpty() ? State.FilePath : InPath;
	if ( Path.IsEmpty() )
	{
		SetStatus( State, "Enter a file name (Save as)", true );
		return false;
	}
	if ( IsReadOnlyPath( Path ) )
	{
		SetStatus( State, "'" + Path + "' is built in and read-only: use Save as", true );
		return false;
	}
	if ( !Path.EndsWith( ".gxmaterial" ) )
	{
		SetStatus( State, "The file name must end with .gxmaterial", true );
		return false;
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		SetStatus( State, "The asset system is not available", true );
		return false;
	}
	const FString Full = Assets->GetRootDirectory() / Path;
	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 )
	{
		NFileSystem::EnsureDirectory( Full.Left( Slash ) );
	}
	const FString Json = State.Definition.ToJson();
	if ( !NFile::WriteAllBytes( Full, Json.GetData(), static_cast<usize>( Json.Len() ) ) )
	{
		SetStatus( State, "Cannot write '" + Full + "'", true );
		return false;
	}
	State.FilePath = Path;
	State.SaveAsPath = Path;
	State.bDirty = false;
	State.LastScanTime = -100.0;
	SetStatus( State, "Saved " + Path, false );
	return true;
}

int32 NMaterialEditorUI::AddNodeWithDefaults( FMaterialEditorState& State, EMaterialNodeKind Kind, const ImVec2& WorldPosition )
{
	FMaterialGraph& Graph = State.Definition.Graph;
	const int32 Id = Graph.AddNode( Kind, FString(), FVector4( 0.0f, 0.0f, 0.0f, 0.0f ), FVector2( WorldPosition.x, WorldPosition.y ) );
	FMaterialNode* Node = Graph.FindNode( Id );
	switch ( Kind )
	{
	case EMaterialNodeKind::Constant:
		Node->Value = FVector4( 0.5f, 0.5f, 0.5f, 1.0f );
		break;
	case EMaterialNodeKind::ScalarParameter:
		Node->Name = UniqueName( State, "Scalar" );
		Node->Value = FVector4( 1.0f, 0.0f, 0.0f, 0.0f );
		break;
	case EMaterialNodeKind::VectorParameter:
		Node->Name = UniqueName( State, "Color" );
		Node->Value = FVector4( 1.0f, 1.0f, 1.0f, 1.0f );
		Node->bColor = true;
		break;
	case EMaterialNodeKind::ParameterCollectionScalar:
	case EMaterialNodeKind::ParameterCollectionVector:
		Node->ParameterCollection = "Collection";
		Node->Name = "Value";
		break;
	case EMaterialNodeKind::TextureSample:
		Node->Name = UniqueName( State, "Texture" );
		break;
	case EMaterialNodeKind::Mask:
		Node->Name = "xyz";
		break;
	case EMaterialNodeKind::Panner:
		Node->Value = FVector4( 0.1f, 0.0f, 0.0f, 0.0f );
		break;
	case EMaterialNodeKind::Tiling:
		Node->Value = FVector4( 1.0f, 1.0f, 0.0f, 0.0f );
		break;
	default:
		break;
	}
	return Id;
}

void NMaterialEditorUI::ArrangeNodes( FMaterialEditorState& State )
{
	FMaterialGraph& Graph = State.Definition.Graph;
	const int32 Count = Graph.Nodes.Num();
	TArray<int32> Level;
	Level.SetNum( Count, -1 );
	auto IndexOf = [&]( int32 Id )
	{
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			if ( Graph.Nodes[Index].Id == Id )
			{
				return Index;
			}
		}
		return -1;
	};
	// 出力へつながるノードが、段 0。そこへ入るノードは、1 つ左の段、と、行き着くまで繰り返す
	for ( int32 Index = 0; Index < Graph.Outputs.Num(); ++Index )
	{
		const int32 Node = IndexOf( Graph.Outputs[Index].SourceNode );
		if ( Node >= 0 )
		{
			Level[Node] = 0;
		}
	}
	for ( int32 Round = 0; Round < Count; ++Round )
	{
		bool bChanged = false;
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			if ( Level[Index] < 0 )
			{
				continue;
			}
			for ( int32 Pin = 0; Pin < Graph.Nodes[Index].Inputs.Num(); ++Pin )
			{
				const int32 Source = IndexOf( Graph.Nodes[Index].Inputs[Pin] );
				if ( Source >= 0 && Level[Source] < Level[Index] + 1 && Level[Index] + 1 <= Count )
				{
					Level[Source] = Level[Index] + 1;
					bChanged = true;
				}
			}
		}
		if ( !bChanged )
		{
			break;
		}
	}
	int32 MaxLevel = 0;
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		MaxLevel = NMath::Max( MaxLevel, Level[Index] );
	}
	// つながらないノードは、いちばん左の、さらに左へ
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		if ( Level[Index] < 0 )
		{
			Level[Index] = MaxLevel + 1;
		}
	}
	TArray<float32> Heights;
	Heights.SetNum( MaxLevel + 2, 0.0f );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		FMaterialNode& Node = Graph.Nodes[Index];
		const int32 Column = Level[Index];
		Node.Position = FVector2( 24.0f + static_cast<float32>( MaxLevel + 1 - Column ) * ( NodeWidth + 44.0f ), 24.0f + Heights[Column] );
		Heights[Column] += NodeSize( NumRows( Node ) ).y + 24.0f;
	}
	State.bDirty = true;
	State.View.bFitted = false;
}

// ---- 描画 ----------------------------------------------------------------------------------------
namespace
{
	// グラフ。変更したら true
	bool DrawGraph( FMaterialEditorState& State, float Height )
	{
		FMaterialGraph& Graph = State.Definition.Graph;
		const TArray<FMaterialOutputPin> OutputPins = NMaterialNodes::GetOutputPins( State.Definition.Domain, State.Definition.Shading );
		bool bChanged = false;

		// 全体の大きさと、Result の位置
		float MinY = 0.0f;
		float MaxX = 0.0f;
		float MaxY = 0.0f;
		for ( int32 Index = 0; Index < Graph.Nodes.Num(); ++Index )
		{
			const FMaterialNode& Node = Graph.Nodes[Index];
			MinY = NMath::Min( MinY, Node.Position.y );
			MaxX = NMath::Max( MaxX, Node.Position.x + NodeWidth );
			MaxY = NMath::Max( MaxY, Node.Position.y + NodeSize( NumRows( Node ) ).y );
		}
		const ImVec2 ResultMin( MaxX + 90.0f, MinY );
		const ImVec2 ResultSize = NodeSize( NMath::Max( 1, OutputPins.Num() ) );
		// グラフ全体の大きさ（原点から）。編集のたびに合わせ直すと、画面が勝手に動くので、合わせたあとは、そのときの大きさのまま（New / Open / Arrange で合わせ直す）
		const ImVec2 MeasuredContent( ResultMin.x + ResultSize.x + 60.0f, NMath::Max( MaxY, ResultMin.y + ResultSize.y ) + 60.0f );
		const ImVec2 Content = State.View.bFitted ? State.View.FittedContent : MeasuredContent;

		FNodeGraphCanvas Canvas;
		State.View.bPanBlocked = State.bPanBlocked;
		if ( !Canvas.Begin( "##materialgraph", State.View, ImVec2( 0.0f, Height ), Content ) )
		{
			return false;
		}
		const ImVec2 Mouse = ImGui::GetMousePos();
		const float Zoom = Canvas.GetZoom();
		State.CanvasCenterWorld = Canvas.ToWorld( ImVec2( ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f ) );
		auto ScreenDistance = [&]( const ImVec2& World )
		{
			const ImVec2 Screen = Canvas.ToScreen( World );
			const float DX = Screen.x - Mouse.x;
			const float DY = Screen.y - Mouse.y;
			return NMath::Sqrt( DX * DX + DY * DY );
		};
		auto InsideNode = [&]( const ImVec2& WorldMin, const ImVec2& WorldSize )
		{
			const ImVec2 A = Canvas.ToScreen( WorldMin );
			const ImVec2 B = Canvas.ToScreen( ImVec2( WorldMin.x + WorldSize.x, WorldMin.y + WorldSize.y ) );
			return Canvas.IsHovered() && Mouse.x >= A.x && Mouse.x <= B.x && Mouse.y >= A.y && Mouse.y <= B.y;
		};

		// 何の上にマウスがあるか（ピンが、ノードの本体より先）
		int32 HoverOutputNode = -1;
		int32 HoverInputNode = -1;
		int32 HoverInputPin = -1;
		int32 HoverResultPin = -1;
		int32 HoverBodyNode = -1;
		for ( int32 Index = Graph.Nodes.Num() - 1; Index >= 0; --Index )
		{
			const FMaterialNode& Node = Graph.Nodes[Index];
			if ( Canvas.IsHovered() && ScreenDistance( OutputPinPosition( Node ) ) < PinHitRadius )
			{
				HoverOutputNode = Node.Id;
			}
			for ( int32 Pin = 0; Pin < Node.Inputs.Num(); ++Pin )
			{
				if ( Canvas.IsHovered() && ScreenDistance( InputPinPosition( Node, Pin ) ) < PinHitRadius )
				{
					HoverInputNode = Node.Id;
					HoverInputPin = Pin;
				}
			}
			if ( HoverBodyNode < 0 && InsideNode( ImVec2( Node.Position.x, Node.Position.y ), NodeSize( NumRows( Node ) ) ) )
			{
				HoverBodyNode = Node.Id;
			}
		}
		for ( int32 Pin = 0; Pin < OutputPins.Num(); ++Pin )
		{
			if ( Canvas.IsHovered() && ScreenDistance( ImVec2( ResultMin.x, ResultMin.y + HeaderHeight + RowHeight * ( static_cast<float>( Pin ) + 0.5f ) ) ) < PinHitRadius )
			{
				HoverResultPin = Pin;
			}
		}
		const bool bOverPin = HoverOutputNode >= 0 || HoverInputNode >= 0 || HoverResultPin >= 0;

		// リンク
		const ImU32 LinkColor = IM_COL32( 190, 195, 210, 255 );
		const ImU32 SelectedLinkColor = IM_COL32( 255, 220, 120, 255 );
		auto DrawLink = [&]( const ImVec2& From, const ImVec2& To, ImU32 Color )
		{
			const float MidX = ( From.x + To.x ) * 0.5f;
			const ImVec2 Route[4] = { From, ImVec2( MidX, From.y ), ImVec2( MidX, To.y ), To };
			Canvas.DrawElbowArrow( Route, 4, Color, 2.0f );
		};
		for ( int32 Index = 0; Index < Graph.Nodes.Num(); ++Index )
		{
			const FMaterialNode& Node = Graph.Nodes[Index];
			for ( int32 Pin = 0; Pin < Node.Inputs.Num(); ++Pin )
			{
				const FMaterialNode* Source = Graph.FindNode( Node.Inputs[Pin] );
				if ( Source != nullptr )
				{
					DrawLink( OutputPinPosition( *Source ), InputPinPosition( Node, Pin ), ( Node.Id == State.SelectedNode || Source->Id == State.SelectedNode ) ? SelectedLinkColor : LinkColor );
				}
			}
		}
		for ( int32 Pin = 0; Pin < OutputPins.Num(); ++Pin )
		{
			const FMaterialNode* Source = Graph.FindNode( Graph.GetOutput( OutputPins[Pin].Name ) );
			if ( Source != nullptr )
			{
				DrawLink( OutputPinPosition( *Source ), ImVec2( ResultMin.x, ResultMin.y + HeaderHeight + RowHeight * ( static_cast<float>( Pin ) + 0.5f ) ), Source->Id == State.SelectedNode ? SelectedLinkColor : LinkColor );
			}
		}
		// つなぎ線を引いている最中
		const FMaterialNode* Dragging = Graph.FindNode( State.DragFromNode );
		if ( Dragging != nullptr )
		{
			Canvas.DrawLine( OutputPinPosition( *Dragging ), Canvas.ToWorld( Mouse ), SelectedLinkColor, 2.0f );
		}

		// ノード
		for ( int32 Index = 0; Index < Graph.Nodes.Num(); ++Index )
		{
			const FMaterialNode& Node = Graph.Nodes[Index];
			const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( Node.Kind );
			const ImVec2 Min( Node.Position.x, Node.Position.y );
			const ImVec2 Size = NodeSize( NumRows( Node ) );
			const bool bSelected = Node.Id == State.SelectedNode;
			const bool bError = IsDiagnosed( State.Diagnostics, Node.Id, EMaterialSeverity::Error );
			const bool bWarning = IsDiagnosed( State.Diagnostics, Node.Id, EMaterialSeverity::Warning );
			const ImU32 Border = bError ? IM_COL32( 235, 70, 70, 255 ) : ( bSelected ? IM_COL32( 255, 255, 255, 255 ) : ( bWarning ? IM_COL32( 230, 190, 70, 255 ) : IM_COL32( 20, 22, 28, 255 ) ) );
			Canvas.DrawBox( Min, Size, IM_COL32( 40, 42, 52, 245 ), Border, bSelected || bError ? 2.5f : 1.5f );
			Canvas.DrawBand( Min, Size.x, HeaderHeight, CategoryColor( Info.Category ) );
			Canvas.DrawText( ImVec2( Min.x + 8.0f, Min.y + 5.0f ), IM_COL32( 245, 247, 252, 255 ), *NodeTitle( Node ), 13.0f, Min, ImVec2( Size.x - 6.0f, HeaderHeight ) );
			// 値を持つノードは、値も小さく見せる
			if ( Info.NumPins == 0 && ( Node.Kind == EMaterialNodeKind::Constant || Node.Kind == EMaterialNodeKind::ScalarParameter || Node.Kind == EMaterialNodeKind::VectorParameter ) )
			{
				const int32 Components = Node.Kind == EMaterialNodeKind::ScalarParameter ? 1 : NMath::Clamp( Node.ValueComponents, 1, 4 );
				const float32 Values[4] = { Node.Value.x, Node.Value.y, Node.Value.z, Node.Value.w };
				FString Text;
				for ( int32 Component = 0; Component < Components; ++Component )
				{
					Text += FString::Printf( Component == 0 ? "%.3g" : ", %.3g", Values[Component] );
				}
				Canvas.DrawText( ImVec2( Min.x + 10.0f, Min.y + HeaderHeight + 4.0f ), IM_COL32( 200, 205, 220, 255 ), *Text, 12.0f, Min, Size );
			}
			for ( int32 Pin = 0; Pin < Info.NumPins; ++Pin )
			{
				const ImVec2 Position = InputPinPosition( Node, Pin );
				const bool bLinked = Pin < Node.Inputs.Num() && Node.Inputs[Pin] >= 0;
				const bool bHot = HoverInputNode == Node.Id && HoverInputPin == Pin;
				Canvas.DrawRelay( Position, bHot ? IM_COL32( 255, 255, 255, 255 ) : ( bLinked ? IM_COL32( 120, 200, 255, 255 ) : IM_COL32( 130, 135, 150, 255 ) ), bHot ? 6.5f : 5.0f );
				FString Label = Info.Pins[Pin].Name;
				if ( !bLinked )
				{
					const float32 Default = Pin < Node.PinDefaults.Num() ? Node.PinDefaults[Pin] : Info.Pins[Pin].Default;
					Label += Info.Pins[Pin].bUvDefault ? FString( " (UV)" ) : FString::Printf( " = %.3g", Default );
				}
				Canvas.DrawText( ImVec2( Position.x + 10.0f, Position.y - 7.0f ), bLinked ? IM_COL32( 225, 228, 238, 255 ) : IM_COL32( 160, 165, 180, 255 ), *Label, 12.0f, Min, Size );
			}
			const bool bOutHot = HoverOutputNode == Node.Id || State.DragFromNode == Node.Id;
			Canvas.DrawRelay( OutputPinPosition( Node ), bOutHot ? IM_COL32( 255, 255, 255, 255 ) : IM_COL32( 255, 190, 90, 255 ), bOutHot ? 6.5f : 5.0f );
		}

		// Result（出力のピン）
		Canvas.DrawBox( ResultMin, ResultSize, IM_COL32( 46, 40, 44, 245 ), IM_COL32( 150, 60, 70, 255 ), 2.0f );
		Canvas.DrawBand( ResultMin, ResultSize.x, HeaderHeight, IM_COL32( 150, 60, 70, 255 ) );
		Canvas.DrawText( ImVec2( ResultMin.x + 8.0f, ResultMin.y + 5.0f ), IM_COL32( 245, 247, 252, 255 ), FString::Printf( "Result (%s)", NMaterial::ToString( State.Definition.Domain ) ).GetData(), 13.0f );
		for ( int32 Pin = 0; Pin < OutputPins.Num(); ++Pin )
		{
			const ImVec2 Position( ResultMin.x, ResultMin.y + HeaderHeight + RowHeight * ( static_cast<float>( Pin ) + 0.5f ) );
			const bool bLinked = Graph.GetOutput( OutputPins[Pin].Name ) >= 0;
			const bool bHot = HoverResultPin == Pin;
			Canvas.DrawRelay( Position, bHot ? IM_COL32( 255, 255, 255, 255 ) : ( bLinked ? IM_COL32( 120, 200, 255, 255 ) : IM_COL32( 130, 135, 150, 255 ) ), bHot ? 6.5f : 5.0f );
			Canvas.DrawText( ImVec2( Position.x + 10.0f, Position.y - 7.0f ), bLinked ? IM_COL32( 225, 228, 238, 255 ) : IM_COL32( 160, 165, 180, 255 ), FString::Printf( "%s (%s)", OutputPins[Pin].Name, NMaterial::ToString( OutputPins[Pin].Type ) ).GetData(), 12.0f );
		}

		// 操作
		if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && Canvas.IsHovered() )
		{
			if ( HoverOutputNode >= 0 )
			{
				State.DragFromNode = HoverOutputNode;
			}
			else if ( HoverInputNode >= 0 )
			{
				// つないである入力のピンを押すと、線を外して、つなぎ元から引き直す
				FMaterialNode* Node = Graph.FindNode( HoverInputNode );
				if ( Node != nullptr && HoverInputPin < Node->Inputs.Num() && Node->Inputs[HoverInputPin] >= 0 )
				{
					State.DragFromNode = Node->Inputs[HoverInputPin];
					Graph.Connect( HoverInputNode, HoverInputPin, -1 );
					bChanged = true;
				}
			}
			else if ( HoverResultPin >= 0 )
			{
				const int32 Source = Graph.GetOutput( OutputPins[HoverResultPin].Name );
				if ( Source >= 0 )
				{
					State.DragFromNode = Source;
					Graph.SetOutput( OutputPins[HoverResultPin].Name, -1 );
					bChanged = true;
				}
			}
			else if ( HoverBodyNode >= 0 )
			{
				State.SelectedNode = HoverBodyNode;
				State.MovingNode = HoverBodyNode;
			}
			else
			{
				State.SelectedNode = -1;
			}
		}
		if ( State.MovingNode >= 0 && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			FMaterialNode* Node = Graph.FindNode( State.MovingNode );
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			if ( Node != nullptr && ( Delta.x != 0.0f || Delta.y != 0.0f ) )
			{
				Node->Position = FVector2( Node->Position.x + Delta.x / Zoom, Node->Position.y + Delta.y / Zoom );
				State.bDirty = true;
			}
		}
		if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
		{
			if ( State.DragFromNode >= 0 )
			{
				if ( HoverInputNode >= 0 && HoverInputNode != State.DragFromNode )
				{
					Graph.Connect( HoverInputNode, HoverInputPin, State.DragFromNode );
					bChanged = true;
				}
				else if ( HoverResultPin >= 0 )
				{
					Graph.SetOutput( OutputPins[HoverResultPin].Name, State.DragFromNode );
					bChanged = true;
				}
			}
			State.DragFromNode = -1;
			State.MovingNode = -1;
		}
		// 右クリック: ノードを足す（ノードの上なら、そのノードを選ぶ）
		if ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && Canvas.IsHovered() )
		{
			if ( HoverBodyNode >= 0 )
			{
				State.SelectedNode = HoverBodyNode;
			}
			else
			{
				State.PopupWorld = Canvas.ToWorld( Mouse );
				ImGui::OpenPopup( "##addnode" );
			}
		}
		if ( ImGui::IsKeyPressed( ImGuiKey_Delete ) && State.SelectedNode >= 0 && Canvas.IsHovered() && !ImGui::GetIO().WantTextInput )
		{
			Graph.RemoveNode( State.SelectedNode );
			State.SelectedNode = -1;
			bChanged = true;
		}
		// 次のフレームのパンを、止めるか: ノードやピンの上、動かしている・つないでいる最中
		State.bPanBlocked = bOverPin || HoverBodyNode >= 0 || State.MovingNode >= 0 || State.DragFromNode >= 0;
		Canvas.End();

		if ( ImGui::BeginPopup( "##addnode" ) )
		{
			EMaterialNodeKind Kind = EMaterialNodeKind::Constant;
			if ( DrawPalette( State, Kind ) )
			{
				State.SelectedNode = NMaterialEditorUI::AddNodeWithDefaults( State, Kind, State.PopupWorld );
				bChanged = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		return bChanged;
	}

	// 選んだノードの設定。変更したら true
	bool DrawDetails( FMaterialEditorState& State )
	{
		FMaterialNode* Node = State.Definition.Graph.FindNode( State.SelectedNode );
		if ( Node == nullptr )
		{
			ImGui::TextDisabled( "Select a node to edit it." );
			return false;
		}
		bool bChanged = false;
		const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( Node->Kind );
		ImGui::Text( "%s  (id %d)", Info.Name, Node->Id );
		ImGui::Separator();
		const bool bHasName = Node->Kind == EMaterialNodeKind::ScalarParameter || Node->Kind == EMaterialNodeKind::VectorParameter || Node->Kind == EMaterialNodeKind::ParameterCollectionScalar
							  || Node->Kind == EMaterialNodeKind::ParameterCollectionVector || Node->Kind == EMaterialNodeKind::TextureSample || Node->Kind == EMaterialNodeKind::Mask;
		if ( bHasName )
		{
			const char* Label = Node->Kind == EMaterialNodeKind::Mask ? "Letters (xyzw / rgba)" : "Name";
			bChanged = NDebugUI::InputString( Label, Node->Name ) || bChanged;
		}
		if ( Node->Kind == EMaterialNodeKind::ParameterCollectionScalar || Node->Kind == EMaterialNodeKind::ParameterCollectionVector )
		{
			bChanged = NDebugUI::InputString( "Parameter collection", Node->ParameterCollection ) || bChanged;
		}
		if ( Node->Kind == EMaterialNodeKind::TextureSample )
		{
			bChanged = NDebugUI::InputString( "Default texture", Node->TexturePath ) || bChanged;
		}
		const bool bVector = Node->Kind == EMaterialNodeKind::Constant || Node->Kind == EMaterialNodeKind::VectorParameter || Node->Kind == EMaterialNodeKind::ParameterCollectionVector;
		if ( bVector )
		{
			int32 Components = NMath::Clamp( Node->ValueComponents, 1, 4 );
			if ( ImGui::SliderInt( "Components", &Components, 1, 4 ) )
			{
				Node->ValueComponents = Components;
				bChanged = true;
			}
		}
		if ( Node->Kind == EMaterialNodeKind::VectorParameter )
		{
			bChanged = ImGui::Checkbox( "Color", &Node->bColor ) || bChanged;
		}
		const bool bScalar = Node->Kind == EMaterialNodeKind::ScalarParameter || Node->Kind == EMaterialNodeKind::ParameterCollectionScalar;
		if ( bScalar )
		{
			bChanged = ImGui::DragFloat( "Default", &Node->Value.x, 0.01f ) || bChanged;
		}
		else if ( bVector )
		{
			float Values[4] = { Node->Value.x, Node->Value.y, Node->Value.z, Node->Value.w };
			const int32 Components = NMath::Clamp( Node->ValueComponents, 1, 4 );
			bool bValueChanged = false;
			if ( Node->bColor && Components >= 3 )
			{
				bValueChanged = Components == 3 ? ImGui::ColorEdit3( "Value", Values ) : ImGui::ColorEdit4( "Value", Values );
			}
			else
			{
				bValueChanged = ImGui::DragScalarN( "Value", ImGuiDataType_Float, Values, Components, 0.01f );
			}
			if ( bValueChanged )
			{
				Node->Value = FVector4( Values[0], Values[1], Values[2], Values[3] );
				bChanged = true;
			}
		}
		if ( Node->Kind == EMaterialNodeKind::Panner )
		{
			float Speed[2] = { Node->Value.x, Node->Value.y };
			if ( ImGui::DragFloat2( "Speed (per second)", Speed, 0.01f ) )
			{
				Node->Value = FVector4( Speed[0], Speed[1], 0.0f, 0.0f );
				bChanged = true;
			}
		}
		if ( Node->Kind == EMaterialNodeKind::Tiling )
		{
			float Tiling[4] = { Node->Value.x, Node->Value.y, Node->Value.z, Node->Value.w };
			if ( ImGui::DragFloat4( "Scale xy, offset xy", Tiling, 0.01f ) )
			{
				Node->Value = FVector4( Tiling[0], Tiling[1], Tiling[2], Tiling[3] );
				bChanged = true;
			}
		}
		// つないでいないピンの数
		bool bHeader = false;
		for ( int32 Pin = 0; Pin < Info.NumPins; ++Pin )
		{
			const bool bLinked = Pin < Node->Inputs.Num() && Node->Inputs[Pin] >= 0;
			if ( bLinked || Info.Pins[Pin].bUvDefault || Info.Pins[Pin].bOptional || Info.Pins[Pin].Expect == EMaterialPinExpect::Slab )
			{
				continue;
			}
			if ( !bHeader )
			{
				ImGui::Separator();
				ImGui::TextDisabled( "Unlinked pins" );
				bHeader = true;
			}
			while ( Node->PinDefaults.Num() < Info.NumPins )
			{
				Node->PinDefaults.Add( Info.Pins[Node->PinDefaults.Num()].Default );
			}
			bChanged = ImGui::DragFloat( Info.Pins[Pin].Name, &Node->PinDefaults[Pin], 0.01f ) || bChanged;
		}
		ImGui::Separator();
		if ( ImGui::Button( "Delete node" ) )
		{
			State.Definition.Graph.RemoveNode( Node->Id );
			State.SelectedNode = -1;
			bChanged = true;
		}
		return bChanged;
	}

	// パラメーター（プレビューの値）
	void DrawParameters( FMaterialEditorState& State )
	{
		if ( !State.Material )
		{
			return;
		}
		const FMaterialProgram& Program = State.Material->GetProgram();
		bool bAny = false;
		for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
		{
			const FMaterialParamSlot& Slot = Program.Params[Index];
			if ( Slot.IsFromParameterCollection() )
			{
				ImGui::TextDisabled( "%s.%s (collection)", *Slot.ParameterCollection, *Slot.Name );
				bAny = true;
				continue;
			}
			bAny = true;
			if ( Slot.Type == EMaterialValueType::Float )
			{
				float32 Value = 0.0f;
				State.Material->GetScalar( Slot.Name, Value );
				if ( ImGui::DragFloat( *Slot.Name, &Value, 0.01f ) )
				{
					State.Material->SetScalar( Slot.Name, Value );
				}
			}
			else
			{
				FVector4 Value;
				State.Material->GetVector( Slot.Name, Value );
				float Values[4] = { Value.x, Value.y, Value.z, Value.w };
				const int32 Components = NMaterial::NumComponents( Slot.Type );
				const bool bEdited = Slot.bColor && Components >= 3 ? ( Components == 3 ? ImGui::ColorEdit3( *Slot.Name, Values ) : ImGui::ColorEdit4( *Slot.Name, Values ) ) : ImGui::DragScalarN( *Slot.Name, ImGuiDataType_Float, Values, Components, 0.01f );
				if ( bEdited )
				{
					State.Material->SetVector( Slot.Name, FVector4( Values[0], Values[1], Values[2], Values[3] ) );
				}
			}
		}
		for ( int32 Index = 0; Index < Program.Textures.Num(); ++Index )
		{
			if ( Program.Textures[Index].Source == EMaterialTextureSource::Parameter )
			{
				ImGui::TextDisabled( "%s (texture t%d%s)", *Program.Textures[Index].Name, Program.Textures[Index].Register, Program.Textures[Index].DefaultPath.IsEmpty() ? ": none" : "" );
				bAny = true;
			}
		}
		if ( !bAny )
		{
			ImGui::TextDisabled( "No parameters." );
		}
	}

	void DrawDiagnostics( FMaterialEditorState& State )
	{
		if ( State.Diagnostics.IsEmpty() )
		{
			ImGui::TextColored( ImVec4( 0.5f, 0.85f, 0.55f, 1.0f ), "No problems." );
			return;
		}
		for ( int32 Index = 0; Index < State.Diagnostics.Num(); ++Index )
		{
			const FMaterialDiagnostic& Diagnostic = State.Diagnostics[Index];
			const ImVec4 Color = Diagnostic.Severity == EMaterialSeverity::Error ? ImVec4( 1.0f, 0.45f, 0.45f, 1.0f ) : ( Diagnostic.Severity == EMaterialSeverity::Warning ? ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ) : ImVec4( 0.7f, 0.78f, 0.9f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_Text, Color );
			ImGui::PushID( Index );
			const FString Text = FString::Printf( "[%s] %s%s", NMaterial::ToString( Diagnostic.Severity ), *Diagnostic.Message, Diagnostic.NodeId >= 0 ? FString::Printf( "  (node %d)", Diagnostic.NodeId ).GetData() : "" );
			if ( ImGui::Selectable( *Text ) && Diagnostic.NodeId >= 0 )
			{
				const FMaterialNode* Node = State.Definition.Graph.FindNode( Diagnostic.NodeId );
				if ( Node != nullptr )
				{
					State.SelectedNode = Node->Id;
					State.View.bCenterRequested = true;
					State.View.CenterWorld = ImVec2( Node->Position.x + NodeWidth * 0.5f, Node->Position.y + 30.0f );
				}
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
		}
	}

	void DrawPreview( FMaterialEditorState& State )
	{
		if ( State.Definition.Domain == EMaterialDomain::Surface )
		{
			ImGui::Combo( "Shape", &State.PreviewShape, "Sphere\0Cube\0Plane\0" );
			ImGui::Checkbox( "Spin", &State.bPreviewSpin );
		}
		else if ( State.Definition.Domain == EMaterialDomain::UI )
		{
			ImGui::SliderFloat( "Hover", &State.PreviewFrame.Hover, 0.0f, 1.0f );
			ImGui::SliderFloat( "Press", &State.PreviewFrame.Press, 0.0f, 1.0f );
			ImGui::SliderFloat( "Value", &State.PreviewFrame.Value, 0.0f, 1.0f );
		}
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		const float Side = NMath::Min( ImGui::GetContentRegionAvail().x, static_cast<float>( FMaterialPreviewSurface::Size ) );
		if ( Executor == nullptr || !State.Material || !State.Material->IsValid() )
		{
			ImGui::Dummy( ImVec2( Side, Side ) );
			ImGui::TextDisabled( State.Material && !State.Material->IsValid() ? "The material has errors." : "No preview in this backend." );
			return;
		}
		State.PreviewFrame.Time = static_cast<float32>( ImGui::GetTime() );
		if ( State.bPreviewSpin )
		{
			State.PreviewYaw += ImGui::GetIO().DeltaTime * 0.6f;
		}
		const bool bRendered = State.Preview.Render( *State.Material, static_cast<EMaterialPreviewShape>( State.PreviewShape ), State.PreviewYaw, State.PreviewPitch, State.PreviewFrame, *Executor );
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy( ImVec2( Side, Side ) );
		if ( bRendered && State.Preview.GetHandle() >= 0 )
		{
			ImGui::GetWindowDrawList()->AddImage( NDebugUI::GetImGuiTexture( State.Preview.GetHandle() ), Origin, ImVec2( Origin.x + Side, Origin.y + Side ) );
		}
		else
		{
			ImGui::TextDisabled( "The preview could not be drawn (see the problems list)." );
		}
		// ドラッグで回す（Surface）
		if ( State.Definition.Domain == EMaterialDomain::Surface && ImGui::IsItemHovered() && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			State.PreviewYaw += ImGui::GetIO().MouseDelta.x * 0.01f;
			State.PreviewPitch = NMath::Clamp( State.PreviewPitch + ImGui::GetIO().MouseDelta.y * 0.01f, -1.3f, 1.3f );
			State.bPreviewSpin = false;
		}
	}
}

void DrawMaterialEditor( FMaterialEditorState& State, bool* bOpen )
{
	if ( !State.bInitialized )
	{
		State.bInitialized = true;
		NMaterialEditorUI::NewMaterial( State, EMaterialDomain::Surface, EMaterialShading::DefaultLit );
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets != nullptr && ImGui::GetTime() - State.LastScanTime > 3.0 )
	{
		State.LastScanTime = ImGui::GetTime();
		State.Files = NFileSystem::ListFilesRecursive( Assets->GetRootDirectory(), { ".gxmaterial" } );
		// エンジン組み込みのマテリアルも、選べるように
		const TArray<FString> Built = NFileSystem::ListFilesRecursive( Assets->GetEngineContentDirectory() / "Materials", { ".gxmaterial" } );
		for ( int32 Index = 0; Index < Built.Num(); ++Index )
		{
			State.Files.Add( "Engine:/Materials/" + Built[Index] );
		}
	}

	ImGui::SetNextWindowSize( ImVec2( 1500.0f, 800.0f ), ImGuiCond_FirstUseEver );
	const FString Title = FString( "Material editor" ) + ( State.bDirty ? "*" : "" ) + "###MaterialEditor";
	if ( !ImGui::Begin( *Title, bOpen ) )
	{
		ImGui::End();
		return;
	}
	bool bChanged = false;

	// 上: ドメイン・ファイル
	int32 Domain = static_cast<int32>( State.Definition.Domain );
	ImGui::SetNextItemWidth( 130.0f );
	if ( ImGui::Combo( "##domain", &Domain, "UI\0Surface\0Particle\0PostProcess\0" ) )
	{
		State.Definition.Domain = static_cast<EMaterialDomain>( Domain );
		bChanged = true;
	}
	ImGui::SameLine();
	if ( State.Definition.Domain == EMaterialDomain::Surface )
	{
		int32 Shading = static_cast<int32>( State.Definition.Shading );
		ImGui::SetNextItemWidth( 120.0f );
		if ( ImGui::Combo( "##shading", &Shading, "Unlit\0DefaultLit\0Substrate\0" ) )
		{
			State.Definition.Shading = static_cast<EMaterialShading>( Shading );
			bChanged = true;
		}
		ImGui::SameLine();
	}
	if ( ImGui::Button( "New" ) )
	{
		NMaterialEditorUI::NewMaterial( State, State.Definition.Domain, State.Definition.Shading );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Save" ) )
	{
		NMaterialEditorUI::SaveMaterial( State, FString() );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 220.0f );
	NDebugUI::InputStringWithHint( "##saveas", "Folder/Name.gxmaterial", State.SaveAsPath );
	ImGui::SameLine();
	if ( ImGui::Button( "Save as" ) )
	{
		NMaterialEditorUI::SaveMaterial( State, State.SaveAsPath );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 260.0f );
	NDebugUI::FileCombo( "##open", State.OpenSelection, State.Files, "(open a material)" );
	ImGui::SameLine();
	if ( ImGui::Button( "Open" ) && !State.OpenSelection.IsEmpty() )
	{
		NMaterialEditorUI::OpenMaterial( State, State.OpenSelection );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Arrange" ) )
	{
		NMaterialEditorUI::ArrangeNodes( State );
	}
	ImGui::Text( "%s%s", State.FilePath.IsEmpty() ? "(unsaved)" : *State.FilePath, State.Material && State.Material->IsValid() ? "" : "  [errors]" );
	if ( !State.StatusMessage.IsEmpty() )
	{
		ImGui::TextColored( State.bStatusIsError ? ImVec4( 1.0f, 0.5f, 0.5f, 1.0f ) : ImVec4( 0.6f, 0.85f, 0.6f, 1.0f ), "%s", *State.StatusMessage );
	}
	ImGui::Separator();

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float LeftWidth = 250.0f;
	const float RightWidth = 310.0f;
	const float DiagnosticsHeight = 130.0f;
	const float GraphWidth = NMath::Max( 200.0f, Avail.x - LeftWidth - RightWidth - 16.0f );

	// 左: ノードの一覧・パラメーター
	if ( ImGui::BeginChild( "##left", ImVec2( LeftWidth, Avail.y ), ImGuiChildFlags_Borders ) )
	{
		ImGui::TextDisabled( "Nodes (or right-click the graph)" );
		EMaterialNodeKind Kind = EMaterialNodeKind::Constant;
		if ( DrawPalette( State, Kind ) )
		{
			State.SelectedNode = NMaterialEditorUI::AddNodeWithDefaults( State, Kind, State.CanvasCenterWorld );
			bChanged = true;
		}
		ImGui::Separator();
		ImGui::TextDisabled( "Parameters (preview values)" );
		DrawParameters( State );
	}
	ImGui::EndChild();
	ImGui::SameLine();

	// 中央: グラフと診断
	ImGui::BeginGroup();
	if ( ImGui::BeginChild( "##graphpanel", ImVec2( GraphWidth, Avail.y - DiagnosticsHeight - 6.0f ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
	{
		bChanged = DrawGraph( State, 0.0f ) || bChanged;
	}
	ImGui::EndChild();
	if ( ImGui::BeginChild( "##diagnostics", ImVec2( GraphWidth, DiagnosticsHeight ), ImGuiChildFlags_Borders ) )
	{
		DrawDiagnostics( State );
	}
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::SameLine();

	// 右: 設定とプレビュー
	if ( ImGui::BeginChild( "##right", ImVec2( RightWidth, Avail.y ), ImGuiChildFlags_Borders ) )
	{
		bChanged = DrawDetails( State ) || bChanged;
		ImGui::Separator();
		DrawPreview( State );
	}
	ImGui::EndChild();

	if ( bChanged )
	{
		MarkChanged( State );
	}
	ImGui::End();
}
