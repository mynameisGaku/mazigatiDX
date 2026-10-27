
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorMeshMaterialPanel.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "Library/Framework/World/WorldRender.h"

//=============================================================================
// ワールドエディタの、編集メッシュ（Mesh 部品）の画面
//   - 描画: メッシュを、実行時と同じ FMeshRenderer（NWorld::MakeMeshRenderer）で描く。内容が変わったときだけ、描く形を作り直す。
//   - 編集中: 頂点・辺・面を重ね描きし（選んだもの・マウスの下のもの）、ツールバー（押し出し・差す・消す・つなぐ・割る・材質）を出す。
//   - Inspector の Mesh 部品: 大きさ・材質の一覧と、編集の始め / 終わり。
//   編集の中身は World/Editor（EditorSession.h の「編集メッシュの編集」）。ここは画面だけ。
//=============================================================================
namespace
{
	using FState = FWorldEditorState;
	using NEditMesh::EEditMeshElement;

	const char* const ElementNames[] = { "Vertex", "Edge", "Face" };

	ImU32 WithAlpha( ImU32 Color, int32 Alpha )
	{
		return ( Color & ~IM_COL32_A_MASK ) | ( static_cast<ImU32>( NMath::Clamp( Alpha, 0, 255 ) ) << IM_COL32_A_SHIFT );
	}

	FColor32 ToColor32( const float ( &Rgb )[3] )
	{
		const auto Byte = [] ( float V ) { return static_cast<uint8>( NMath::Clamp( static_cast<int32>( NMath::Round( V * 255.0f ) ), 0, 255 ) ); };
		return FColor32( Byte( Rgb[0] ), Byte( Rgb[1] ), Byte( Rgb[2] ), 255 );
	}

	void SectionLabel( const char* Text )
	{
		ImGui::Spacing();
		ImGui::TextColored( ImVec4( 0.55f, 0.75f, 1.0f, 1.0f ), "%s", Text );
	}
}

//=============================================================================
// 描く（キャッシュつき）
//=============================================================================
bool NWorldEditorUI::RecordMeshObject( FWorldEditorState& S, const FWorldObject& Object, const FTransform& Placed, FDrawContext& Draw, int32 CacheKey )
{
	if ( !Object.Mesh.IsSet() || !Object.bEnabled ) return false;
	FAssetEditorInfo* Info = S.AssetInfo.Get();
	if ( Info == nullptr ) return false;

	const uint64 Hash = Object.Mesh.GetValue().ComputeHash();
	FState::FMeshView& View = S.MeshViews.FindOrAdd( CacheKey );
	if ( !View.bBuilt || View.Hash != Hash )
	{
		View.Renderer = NWorld::MakeMeshRenderer( Object.Mesh.GetValue(), [Info]( const FString& Path ) { return Info->GetTexture( Path ); } );
		View.Hash = Hash;
		View.bBuilt = true;
	}
	if ( !View.Renderer.IsValid() ) return false;
	View.Renderer.Record( Draw, Placed );
	return true;
}

void NWorldEditorUI::PruneMeshViews( FWorldEditorState& S, const TSet<int32>& Seen )
{
	TArray<int32> Gone;
	for ( const auto& Pair : S.MeshViews )
	{
		if ( !Seen.Contains( Pair.Key ) ) Gone.Add( Pair.Key );
	}
	for ( const int32 Id : Gone ) S.MeshViews.Remove( Id );
}

// 現在ownerが残っているmesh rendererの材質時計を一度だけ進める。
void NWorldEditorUI::UpdateMeshMaterialPreviews( FWorldEditorState& S, float32 DeltaTime )
{
	// viewごとのrenderer clockをRecordSceneより前に更新する。
	for ( auto& Pair : S.MeshViews )
	{
		FWorldEditorState::FMeshView& View = Pair.Value;
		if ( View.bBuilt ) View.Renderer.Update( DeltaTime );
	}
}

//=============================================================================
// 作る・始める・終わる
//=============================================================================
void NWorldEditorUI::CreateMeshAtView( FWorldEditorState& S, EEditMeshPrimitive Kind )
{
	if ( S.Session.IsMeshEditing() ) S.Session.EndMeshEdit();
	S.Session.CreateMesh( Kind, GetViewCenterPlacement( S ) );
}

void NWorldEditorUI::ToggleMeshEdit( FWorldEditorState& S )
{
	CWorldEditorSession& Session = S.Session;
	if ( Session.IsMeshEditing() )
	{
		Session.EndMeshEdit();
		return;
	}
	const int32 Id = Session.GetPrimary();
	if ( Id == WorldNoId || !Session.BeginMeshEdit( Id ) ) SetStatus( S, "Select an object with a Mesh component to edit it (Create > Mesh adds one)", true );
	else SetStatus( S, "Editing the mesh: 1 / 2 / 3 vertex / edge / face, Tab to finish" );
}

//=============================================================================
// 編集中の重ね描き
//=============================================================================
void NWorldEditorUI::DrawMeshOverlay( FWorldEditorState& S, ImDrawList* DrawList, const ImVec2& Origin )
{
	CWorldEditorSession& Session = S.Session;
	const FEditMesh* Mesh = Session.GetEditedMesh();
	if ( Mesh == nullptr ) return;
	const FTransform World = Session.GetEditedWorldTransform();
	const FEditMeshSelection& Selection = Session.GetMeshSelection();
	const EEditMeshElement Element = Session.GetMeshElement();

	// 頂点の画面の位置
	const int32 NumVertices = Mesh->GetNumVertices();
	TArray<ImVec2> Pixels;
	TArray<bool> OnScreen;
	Pixels.Init( ImVec2( 0.0f, 0.0f ), NumVertices );
	OnScreen.Init( false, NumVertices );
	for ( int32 i = 0; i < NumVertices; ++i )
	{
		FVector2 Pixel;
		if ( Session.Camera.WorldToScreen( S.View, World.TransformPoint( Mesh->Vertices[i] ), Pixel ) )
		{
			Pixels[i] = ImVec2( Origin.x + Pixel.x, Origin.y + Pixel.y );
			OnScreen[i] = true;
		}
	}
	const auto Polygon = [&]( const TArray<int32>& Indices, TArray<ImVec2>& Out )
	{
		Out.Reset();
		for ( const int32 Index : Indices )
		{
			if ( !Mesh->Vertices.IsValidIndex( Index ) || !OnScreen[Index] ) return false;
			Out.Add( Pixels[Index] );
		}
		return Out.Num() >= 3;
	};

	// 選んだ面（塗り）と、マウスの下の面
	TArray<ImVec2> Points;
	for ( const int32 Face : Selection.Faces )
	{
		if ( Mesh->Faces.IsValidIndex( Face ) && Polygon( Mesh->Faces[Face].Indices, Points ) ) DrawList->AddConvexPolyFilled( Points.GetData(), Points.Num(), IM_COL32( 255, 150, 40, 90 ) );
	}
	const FEditMeshHit& Hover = S.HoverMeshHit;
	if ( Hover.bHit && Hover.Element == EEditMeshElement::Face && Mesh->Faces.IsValidIndex( Hover.Index ) && Polygon( Mesh->Faces[Hover.Index].Indices, Points ) ) DrawList->AddConvexPolyFilled( Points.GetData(), Points.Num(), IM_COL32( 255, 235, 120, 60 ) );

	// 辺（多いときは、選んだものとマウスの下のものだけ）
	const TArray<int64> Edges = NEditMesh::GetAllEdges( *Mesh );
	const bool bDrawAllEdges = Edges.Num() <= 30000;
	if ( bDrawAllEdges )
	{
		for ( const int64 Edge : Edges )
		{
			int32 A = 0, B = 0;
			NEditMesh::SplitEdgeKey( Edge, A, B );
			if ( !OnScreen[A] || !OnScreen[B] ) continue;
			DrawList->AddLine( Pixels[A], Pixels[B], IM_COL32( 15, 15, 25, 170 ), 3.0f );   // 暗い縁取りの上に明るい線（どの色の面の上でも見える）
			DrawList->AddLine( Pixels[A], Pixels[B], IM_COL32( 235, 240, 255, 230 ), 1.2f );
		}
	}
	for ( const int64 Edge : Selection.Edges )
	{
		int32 A = 0, B = 0;
		NEditMesh::SplitEdgeKey( Edge, A, B );
		if ( Mesh->Vertices.IsValidIndex( A ) && Mesh->Vertices.IsValidIndex( B ) && OnScreen[A] && OnScreen[B] ) DrawList->AddLine( Pixels[A], Pixels[B], NWorldEditorUI::ColorSelected, 3.0f );
	}
	if ( Hover.bHit && Hover.Element == EEditMeshElement::Edge )
	{
		int32 A = 0, B = 0;
		NEditMesh::SplitEdgeKey( Hover.Edge, A, B );
		if ( Mesh->Vertices.IsValidIndex( A ) && Mesh->Vertices.IsValidIndex( B ) && OnScreen[A] && OnScreen[B] ) DrawList->AddLine( Pixels[A], Pixels[B], NWorldEditorUI::ColorHighlight, 3.0f );
	}
	if ( Hover.bHit && Hover.Element == EEditMeshElement::Face && Mesh->Faces.IsValidIndex( Hover.Index ) && Polygon( Mesh->Faces[Hover.Index].Indices, Points ) )
	{
		for ( int32 i = 0; i < Points.Num(); ++i ) DrawList->AddLine( Points[i], Points[( i + 1 ) % Points.Num()], NWorldEditorUI::ColorHighlight, 2.0f );
	}

	// 頂点（頂点の選択のときは大きく。多いときは、選んだものだけ）
	const bool bVertexMode = Element == EEditMeshElement::Vertex;
	if ( NumVertices <= 8000 )
	{
		TArray<bool> Used;
		Used.Init( false, NumVertices );
		for ( int32 Face = 0; Face < Mesh->GetNumFaces(); ++Face )
		{
			if ( !Mesh->IsFaceValid( Face ) ) continue;
			for ( const int32 Index : Mesh->Faces[Face].Indices ) Used[Index] = true;
		}
		for ( int32 i = 0; i < NumVertices; ++i )
		{
			if ( !Used[i] || !OnScreen[i] ) continue;
			DrawList->AddCircleFilled( Pixels[i], bVertexMode ? 4.5f : 3.0f, IM_COL32( 15, 15, 25, 200 ) );
			DrawList->AddCircleFilled( Pixels[i], bVertexMode ? 3.0f : 2.0f, IM_COL32( 245, 248, 255, 255 ) );
		}
	}
	for ( const int32 Vertex : NEditMesh::GetAffectedVertices( *Mesh, Selection ) )
	{
		if ( OnScreen[Vertex] ) DrawList->AddCircleFilled( Pixels[Vertex], bVertexMode ? 5.0f : 3.0f, NWorldEditorUI::ColorSelected );
	}
	if ( Hover.bHit && Hover.Element == EEditMeshElement::Vertex && Mesh->Vertices.IsValidIndex( Hover.Index ) && OnScreen[Hover.Index] ) DrawList->AddCircle( Pixels[Hover.Index], 8.0f, NWorldEditorUI::ColorHighlight, 16, 2.0f );

	// 左上の案内
	const int32 NumChosen = Element == EEditMeshElement::Vertex ? Selection.Vertices.Num() : Element == EEditMeshElement::Edge ? Selection.Edges.Num() : Selection.Faces.Num();
	const FString Text = FString::Printf( "Mesh edit: %s mode, %d chosen   (1 2 3: vertex edge face   Tab: finish   Del: delete   L: linked   Ctrl+A: all)", ElementNames[static_cast<int32>( Element )], NumChosen );
	// 左下（軸の目印の右）
	DrawList->AddText( ImVec2( Origin.x + 96.0f, Origin.y + S.ViewSize.y - 20.0f ), IM_COL32( 0, 0, 0, 220 ), UI_TEXT( Text ) );
	DrawList->AddText( ImVec2( Origin.x + 95.0f, Origin.y + S.ViewSize.y - 21.0f ), IM_COL32( 255, 205, 90, 245 ), UI_TEXT( Text ) );
}

//=============================================================================
// 編集中のツールバー
//=============================================================================
void NWorldEditorUI::DrawMeshToolbar( FWorldEditorState& S )
{
	CWorldEditorSession& Session = S.Session;
	if ( !Session.IsMeshEditing() ) return;
	const bool bHasChosen = !Session.GetMeshSelection().IsEmpty();
	const bool bBusy = Session.IsGizmoDragging();

	// 前の部品の右に続けて置く。右端に収まらなければ、次の行に折り返す（窓が狭くても、はみ出さない）
	const auto Continue = [] ( float NextWidth )
	{
		const ImGuiStyle& Style = ImGui::GetStyle();
		const float Right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - Style.WindowPadding.x;
		if ( ImGui::GetItemRectMax().x + Style.ItemSpacing.x + NextWidth <= Right ) ImGui::SameLine();
	};
	const auto ButtonWidth = [] ( const char* Label ) { return ImGui::CalcTextSize( Label ).x + ImGui::GetStyle().FramePadding.x * 2.0f; };
	const float FieldHeight = ImGui::GetFrameHeight();

	// 選ぶ要素
	for ( int32 i = 0; i < 3; ++i )
	{
		const bool bActive = static_cast<int32>( Session.GetMeshElement() ) == i;
		if ( bActive ) ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
		if ( ImGui::Button( ElementNames[i] ) ) Session.SetMeshElement( static_cast<EEditMeshElement>( i ) );
		if ( bActive ) ImGui::PopStyleColor();
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Choose %ss  [%d]", ElementNames[i], i + 1 );
		Continue( ButtonWidth( ElementNames[( i + 1 ) % 3] ) );
	}

	ImGui::BeginDisabled( bBusy );
	if ( ImGui::Button( "All" ) ) Session.MeshSelectAll();
	Continue( ButtonWidth( "None" ) );
	if ( ImGui::Button( "None" ) ) Session.MeshSelectNone();
	Continue( ButtonWidth( "Invert" ) );
	if ( ImGui::Button( "Invert" ) ) Session.MeshInvertSelection();
	Continue( ButtonWidth( "Linked" ) );
	ImGui::BeginDisabled( !bHasChosen );
	if ( ImGui::Button( "Linked" ) ) Session.MeshSelectLinked();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Add the whole connected piece  [L]" );
	ImGui::EndDisabled();
	Continue( ButtonWidth( "Done" ) );
	if ( ImGui::Button( "Done" ) ) Session.EndMeshEdit();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Finish editing the mesh  [Tab]" );

	// 形を変える操作（選んだものが無いと、押せない）
	ImGui::BeginDisabled( !bHasChosen );
	ImGui::SetNextItemWidth( 56.0f );
	ImGui::DragFloat( "##ExtrudeDistance", &S.MeshExtrudeDistance, 0.02f, -100.0f, 100.0f, "%.2f" );
	Continue( ButtonWidth( "Extrude" ) );
	if ( ImGui::Button( "Extrude" ) && !Session.MeshExtrude( S.MeshExtrudeDistance ) ) SetStatus( S, "Choose faces (or all 4 corners of a face) to extrude", true );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Push the chosen faces out by this distance (negative digs in). Then drag the gizmo to shape it" );
	Continue( 48.0f );
	ImGui::SetNextItemWidth( 48.0f );
	ImGui::DragFloat( "##InsetAmount", &S.MeshInsetAmount, 0.01f, 0.0f, 0.95f, "%.2f" );
	Continue( ButtonWidth( "Inset" ) );
	if ( ImGui::Button( "Inset" ) && !Session.MeshInset( S.MeshInsetAmount ) ) SetStatus( S, "Choose faces to inset", true );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Shrink each chosen face towards its middle, leaving a frame (0..0.95)" );
	Continue( ButtonWidth( "Delete" ) );
	if ( ImGui::Button( "Delete" ) ) Session.MeshDeleteSelection();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Delete the chosen faces / the faces round the chosen vertices or edges  [Del]" );
	Continue( ButtonWidth( "Flip" ) );
	if ( ImGui::Button( "Flip" ) ) Session.MeshFlip();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Turn the chosen faces round (front <-> back)" );
	Continue( ButtonWidth( "Merge" ) );
	if ( ImGui::Button( "Merge" ) ) Session.MeshMerge();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Join the chosen vertices into one, in the middle" );
	Continue( ButtonWidth( "Split" ) );
	if ( ImGui::Button( "Split" ) ) Session.MeshSubdivide();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Cut each chosen face into quads (a vertex in the middle of the face and of each edge)" );
	Continue( ButtonWidth( "Triangulate" ) );
	if ( ImGui::Button( "Triangulate" ) ) Session.MeshTriangulate();
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Cut the chosen faces into triangles (fixes faces that are not convex)" );
	ImGui::EndDisabled();
	Continue( 60.0f );
	ImGui::SetNextItemWidth( 60.0f );
	ImGui::DragFloat( "##WeldDistance", &S.MeshWeldDistance, 0.001f, 0.0001f, 10.0f, "%.4f" );
	Continue( ButtonWidth( "Weld" ) );
	if ( ImGui::Button( "Weld" ) && !Session.MeshWeld( S.MeshWeldDistance ) ) SetStatus( S, "Nothing is that close together" );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Join vertices closer than this (the chosen ones, or all of them when nothing is chosen)" );

	// 基本の形を足す
	Continue( 86.0f + FieldHeight );
	ImGui::SetNextItemWidth( 86.0f );
	ImGui::Combo( "##AppendKind", &S.MeshAppendKind, "Box\0Plane\0Cylinder\0Sphere\0Ramp\0Stairs\0" );
	Continue( ButtonWidth( "Add shape" ) );
	if ( ImGui::Button( "Add shape" ) ) Session.MeshAppendPrimitive( static_cast<EEditMeshPrimitive>( S.MeshAppendKind ), GetViewCenterPlacement( S ) );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Add this shape to the mesh, where the view is looking" );
	ImGui::EndDisabled();
}

//=============================================================================
// Inspector の Mesh 部品
//=============================================================================
void NWorldEditorUI::DrawMeshComponentBody( FWorldEditorState& S, const FWorldObject& Object )
{
	CWorldEditorSession& Session = S.Session;
	const int32 Id = Object.Id;
	const FEditMesh& Mesh = Object.Mesh.GetValue();
	const bool bEditing = Session.GetMeshObjectId() == Id;

	FVector3 Min = FVector3::Zero, Max = FVector3::Zero;
	const bool bHasBounds = Mesh.GetBounds( Min, Max );
	ImGui::TextDisabled( "%d vertices, %d faces (%d triangles)", Mesh.GetNumVertices(), Mesh.GetNumFaces(), Mesh.GetNumTriangles() );
	if ( bHasBounds ) ImGui::TextDisabled( "size %.2f x %.2f x %.2f", Max.x - Min.x, Max.y - Min.y, Max.z - Min.z );
	const int32 NumOpen = NEditMesh::CountOpenEdges( Mesh );
	if ( NumOpen > 0 ) ImGui::TextDisabled( "%d open edge(s) (holes, or T-junctions)", NumOpen );

	if ( ImGui::Button( bEditing ? "Finish editing  [Tab]" : "Edit mesh  [Tab]" ) )
	{
		if ( bEditing ) Session.EndMeshEdit();
		else Session.BeginMeshEdit( Id );
	}
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Move vertices, edges and faces with the gizmo; extrude, inset, delete, merge and split them" );

	float32 Smooth = Mesh.SmoothAngle;
	ImGui::SetNextItemWidth( 120.0f );
	if ( ImGui::SliderFloat( "Smooth angle", &Smooth, 0.0f, 90.0f, "%.0f deg" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Mesh.Smooth" )->Mesh.GetValue().SmoothAngle = Smooth;
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Faces whose angle is within this are shaded as one smooth surface (0 = every face flat)" );

	// 材質
	SectionLabel( "Materials" );
	if ( Mesh.Materials.IsEmpty() ) ImGui::TextDisabled( "(none: every face uses the default grey)" );
	int32 RemoveIndex = -1;
	for ( int32 i = 0; i < Mesh.Materials.Num(); ++i )
	{
		ImGui::PushID( i );
		// UI変更でmaterial配列参照を保持しないslot設定値。
		const FEditMeshMaterial Material = Mesh.Materials[i];
		FString Title = FString::Printf( "%d  %s", i, Material.Name.IsEmpty() ? "(unnamed)" : *Material.Name );
		const bool bOpen = ImGui::TreeNodeEx( "##Material", ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen, "%s", UI_TEXT( Title ) );
		ImGui::SameLine( ImGui::GetWindowWidth() - 36.0f );
		if ( ImGui::SmallButton( "x" ) ) RemoveIndex = i;
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Remove this material (its faces use material 0)" );
		if ( bOpen )
		{
			FString Name = Material.Name;
			ImGui::SetNextItemWidth( -FLT_MIN );
			if ( NDebugUI::InputStringWithHint( "##Name", "name", Name ) ) NWorldEdit::EditObject( Session.Document, Id, *FString::Printf( "Inspector.Mesh.Name.%d", i ) )->Mesh.GetValue().Materials[i].Name = Name;
			float Color[3] = { Material.Color.r / 255.0f, Material.Color.g / 255.0f, Material.Color.b / 255.0f };
			if ( ImGui::ColorEdit3( "Colour", Color, ImGuiColorEditFlags_NoInputs ) )
			{
				const FColor32 Picked = ToColor32( Color );
				FEditMeshMaterial& Target = NWorldEdit::EditObject( Session.Document, Id, *FString::Printf( "Inspector.Mesh.Color.%d", i ) )->Mesh.GetValue().Materials[i];
				Target.Color = Picked;
			}
			FString Texture = Material.Texture;
			bool bChanged = false;
			ImGui::TextUnformatted( "Image" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			bChanged = NDebugUI::FileCombo( "##Texture", Texture, S.TextureFiles, "(none)" );
			if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Mesh.GetValue().Materials[i].Texture = Texture;
			if ( !Texture.IsEmpty() && S.AssetInfo.IsValid() && !S.AssetInfo->Exists( Texture ) ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "'%s' was not found", UI_TEXT( Texture ) );
			float32 Tile = Material.Tile;
			if ( ImGui::DragFloat( "Tile (m per picture)", &Tile, 0.02f, 0.01f, 1000.0f, "%.2f" ) ) NWorldEdit::EditObject( Session.Document, Id, *FString::Printf( "Inspector.Mesh.Tile.%d", i ) )->Mesh.GetValue().Materials[i].Tile = NMath::Max( Tile, 0.001f );
			float32 Offset[2] = { Material.Offset.x, Material.Offset.y };
			if ( ImGui::DragFloat2( "Offset", Offset, 0.01f ) ) NWorldEdit::EditObject( Session.Document, Id, *FString::Printf( "Inspector.Mesh.Offset.%d", i ) )->Mesh.GetValue().Materials[i].Offset = FVector2( Offset[0], Offset[1] );
			bool bTwoSided = Material.bTwoSided;
			if ( ImGui::Checkbox( "Two-sided", &bTwoSided ) ) NWorldEdit::EditObject( Session.Document, Id )->Mesh.GetValue().Materials[i].bTwoSided = bTwoSided;
			if ( bEditing )
			{
				ImGui::BeginDisabled( Session.GetMeshSelection().IsEmpty() );
				if ( ImGui::Button( "Assign to chosen faces" ) ) Session.MeshSetMaterial( i );
				ImGui::EndDisabled();
			}
			ImGui::Separator();
			// 現在のslot効果を一時値で編集し、変更後に一度だけdocumentへ戻す。
			FMeshMaterialEffectDefinition Effect = Material.Effect;
			FString EffectTexture = Material.Texture;
			// viewportが前frameまでに作った選択objectのpreview renderer。
			FWorldEditorState::FMeshView* MeshView = S.MeshViews.Find( Id );
			FMeshRenderer* PreviewRenderer = MeshView != nullptr && MeshView->bBuilt ? &MeshView->Renderer : nullptr;
			// 切替と数値入力の終了を呼出側のworld履歴へ通知する。
			bool bEffectInputEnded = false;
			if ( NWorldEditorUI::DrawMeshMaterialEffectPanel( Effect, EffectTexture, S.TextureFiles, PreviewRenderer, bEffectInputEnded ) )
			{
				FWorldObject* EditedObject = NWorldEdit::EditObject( Session.Document, Id, *FString::Printf( "Inspector.Mesh.MaterialEffect.%d.%d", Id, i ) );
				if ( EditedObject != nullptr && EditedObject->Mesh.IsSet() && EditedObject->Mesh.GetValue().Materials.IsValidIndex( i ) )
				{
					EditedObject->Mesh.GetValue().Materials[i].Effect = Effect;
					EditedObject->Mesh.GetValue().Materials[i].Texture = EffectTexture;
				}
			}
			if ( bEffectInputEnded ) Session.Document.EndEditSession();
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if ( RemoveIndex >= 0 )
	{
		// 同frameの入力で保存された変更を保つ最新documentから削除する。
		const FWorldObject* CurrentObject = Session.Document.Get().FindObject( Id );
		if ( CurrentObject == nullptr || !CurrentObject->Mesh.IsSet() ) return;
		FEditMesh Edited = CurrentObject->Mesh.GetValue();
		NEditMesh::RemoveMaterial( Edited, RemoveIndex );
		NWorldEdit::EditObject( Session.Document, Id )->Mesh = Edited;
	}
	if ( ImGui::Button( "Add material" ) )
	{
		// 同frameの入力で保存された変更を保つ最新documentから追加する。
		const FWorldObject* CurrentObject = Session.Document.Get().FindObject( Id );
		if ( CurrentObject == nullptr || !CurrentObject->Mesh.IsSet() ) return;
		FEditMesh Edited = CurrentObject->Mesh.GetValue();
		FEditMeshMaterial Fresh;
		Fresh.Name = FString::Printf( "Material %d", Edited.Materials.Num() );
		// 材質が 0 個のとき、面は既定の材質を使っている。足した材質が、いまの見た目と同じ 0 番になるように、先に既定の 1 つを足す
		if ( Edited.Materials.IsEmpty() ) Edited.Materials.Add( FEditMeshMaterial() );
		else Fresh.Color = Edited.Materials.Last().Color;
		Edited.Materials.Add( Fresh );
		NWorldEdit::EditObject( Session.Document, Id )->Mesh = Edited;
	}
}
