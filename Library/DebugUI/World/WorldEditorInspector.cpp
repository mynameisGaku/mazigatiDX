#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/DebugUI/Asset/AssetBrowser.h"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/DebugUI/World/WorldEditorInternal.h"
#include "Library/DebugUI/World/WorldEditorModelMaterialPanel.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Runtime/WorldCollision.h"
#include "Library/Framework/World/Definition/WorldHierarchy.h"

//=============================================================================
// インスペクター
//=============================================================================
namespace
{
	using FState = FWorldEditorState;
	using NWorldEditorUI::ObjectLabel;
	using NWorldEditorUI::SectionTitle;

	const char* const BusNames[] = { "Master", "Music", "Sfx", "Ambient", "Voice", "Ui" };
	const char* const PropTypeNames[] = { "Bool", "Int", "Float", "String", "Vector3", "Asset", "Object" };
	const char* const BillboardModeNames[] = { "Camera (always faces the screen)", "CameraY (turns around the Y axis only)", "World (fixed to the object)" };
	const char* const ShapeNames[] = { "Box", "Sphere", "Capsule", "Mesh" };
	const char* const CollisionModeNames[] = { "Auto (world tag rules)", "On", "Off" };

	// 項目を 1 つ、「リセット」の小さなボタンつきで出す
	bool ResetButton( const char* Id )
	{
		ImGui::SameLine();
		const bool bPressed = ImGui::SmallButton( Id );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Reset" );
		}
		return bPressed;
	}

	// 部品の見出し: 右端に削除ボタン。開いているときに true
	bool ComponentHeader( const char* Title, bool& bOutRemove )
	{
		bOutRemove = false;
		const bool bOpen = ImGui::CollapsingHeader( Title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap );
		ImGui::SameLine( ImGui::GetWindowWidth() - 36.0f );
		ImGui::PushID( Title );
		bOutRemove = ImGui::SmallButton( "x" );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Remove this component" );
		}
		ImGui::PopID();
		return bOpen;
	}

	void DrawAssetPathField( FState& S, const char* Label, FString& Path, const TArray<FString>& Files, bool& bOutChanged )
	{
		ImGui::TextUnformatted( Label );
		// 専用のエディタがある種類（.gxeffect / .gxmodelanim / .gxworld など）の欄には、右に Open ボタンを出す（ツール同士の連携）
		EAssetBrowserKind Kind = EAssetBrowserKind::Texture;
		const bool bCanOpen = !Path.IsEmpty() && NDebugUI::TryGetAssetBrowserKind( Path, Kind ) && NDebugUI::HasEditor( Kind ) && S.AssetInfo.IsValid() && S.AssetInfo->Exists( Path );
		ImGui::SetNextItemWidth( bCanOpen ? -56.0f : -FLT_MIN );
		ImGui::PushID( Label );
		bOutChanged = NDebugUI::FileCombo( "##Path", Path, Files, "(none)" );
		if ( bCanOpen )
		{
			ImGui::SameLine();
			if ( ImGui::Button( "Open" ) )
			{
				NDebugUI::RequestOpenAssetInEditor( Path );
			}
			if ( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "Open this file in its editor (%s)", NDebugUI::ToString( Kind ) );
			}
		}
		ImGui::PopID();
		if ( !Path.IsEmpty() && S.AssetInfo.IsValid() && !S.AssetInfo->Exists( Path ) )
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "'%s' was not found", UI_TEXT( Path ) );
		}
	}

	void DrawTransformSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		const int32 Id = Object.Id;
		SectionTitle( "Transform  (relative to the parent)" );

		FVector3 Position = Object.Transform.Position;
		if ( ImGui::DragFloat3( "Position", &Position.x, 0.05f, 0.0f, 0.0f, "%.3f" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id, "Inspector.Position" )->Transform.Position = Position;
		}
		if ( ResetButton( "R##Position" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id )->Transform.Position = FVector3::Zero;
		}

		FVector3 Rotation = Object.Transform.RotationDegrees;
		if ( ImGui::DragFloat3( "Rotation", &Rotation.x, 0.5f, 0.0f, 0.0f, "%.1f" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id, "Inspector.Rotation" )->Transform.RotationDegrees = Rotation;
		}
		if ( ResetButton( "R##Rotation" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id )->Transform.RotationDegrees = FVector3::Zero;
		}

		FVector3 Scale = Object.Transform.Scale;
		if ( ImGui::DragFloat3( "Scale", &Scale.x, 0.01f, 0.0f, 0.0f, "%.3f" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id, "Inspector.Scale" )->Transform.Scale = Scale;
		}
		if ( ResetButton( "R##Scale" ) )
		{
			NWorldEdit::EditObject( Session.Document, Id )->Transform.Scale = FVector3::One;
		}

		const FTransform WorldTransform = NWorld::ComputeWorldTransform( Session.Document.Get(), Id );
		if ( Object.Parent != WorldNoId )
		{
			ImGui::TextDisabled( "World position  %.3f  %.3f  %.3f", WorldTransform.Position.x, WorldTransform.Position.y, WorldTransform.Position.z );
		}
	}

	void DrawTagsSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Tags  (the code finds and binds objects by tag)" );

		const TArray<FString> Tags = Object.Tags;
		for ( int32 i = 0; i < Tags.Num(); ++i )
		{
			ImGui::PushID( i );
			if ( i > 0 && ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize( UI_TEXT( Tags[i] ) ).x + 60.0f )
			{
				ImGui::SameLine();
			}
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.25f, 0.38f, 0.58f, 1.0f ) );
			const FString Chip = Tags[i] + "  x";
			if ( ImGui::SmallButton( UI_TEXT( Chip ) ) )
			{
				NWorldEdit::RemoveTag( Session.Document, Object.Id, Tags[i] );
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		if ( Tags.IsEmpty() )
		{
			ImGui::TextDisabled( "(no tags)" );
		}

		ImGui::SetNextItemWidth( 140.0f );
		const bool bEnter = NDebugUI::InputStringWithHint( "##NewTag", "new tag", S.NewTag, ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if ( ( ImGui::Button( "Add tag" ) || bEnter ) && !S.NewTag.TrimStartAndEnd().IsEmpty() )
		{
			NWorldEdit::AddTag( Session.Document, Object.Id, S.NewTag );
			S.NewTag = FString();
		}

		// すでにワールドにあるタグから選ぶ
		TArray<FString> Known;
		for ( const FWorldObject& Other : Session.Document.Get().Objects )
		{
			for ( const FString& Tag : Other.Tags )
			{
				if ( !Object.HasTag( Tag ) )
				{
					Known.AddUnique( Tag );
				}
			}
		}
		if ( !Known.IsEmpty() )
		{
			Known.Sort();
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 120.0f );
			if ( ImGui::BeginCombo( "##KnownTags", "existing...", ImGuiComboFlags_HeightLarge ) )
			{
				for ( const FString& Tag : Known )
				{
					if ( ImGui::Selectable( UI_TEXT( Tag ) ) )
					{
						NWorldEdit::AddTag( Session.Document, Object.Id, Tag );
					}
				}
				ImGui::EndCombo();
			}
		}
	}

	void DrawPropEditor( FState& S, const FWorldObject& Object, const FWorldProp& Prop )
	{
		CWorldEditorSession& Session = S.Session;
		FWorldProp Edited = Prop;
		const FString Key = "Inspector.Prop." + Prop.Name;
		bool bChanged = false;
		ImGui::SetNextItemWidth( -FLT_MIN );
		switch ( Prop.Type )
		{
		case EWorldPropType::Bool: bChanged = ImGui::Checkbox( "##V", &Edited.bValue ); break;
		case EWorldPropType::Int: bChanged = ImGui::InputScalar( "##V", ImGuiDataType_S64, &Edited.IntValue ); break;
		case EWorldPropType::Float: bChanged = ImGui::DragScalar( "##V", ImGuiDataType_Double, &Edited.FloatValue, 0.01f, nullptr, nullptr, "%.4f" ); break;
		case EWorldPropType::String: bChanged = NDebugUI::InputString( "##V", Edited.StringValue ); break;
		case EWorldPropType::Vector3: bChanged = ImGui::DragFloat3( "##V", &Edited.VectorValue.x, 0.05f ); break;
		case EWorldPropType::Asset:
		{
			// 画像・モデル・音のどれのファイルでも選べるように、全部をまとめた一覧から
			TArray<FString> All = S.ModelFiles;
			All.Append( S.TextureFiles );
			All.Append( S.SoundFiles );
			bChanged = NDebugUI::FileCombo( "##V", Edited.StringValue, All, "(none)" );
			break;
		}
		case EWorldPropType::Object:
		{
			const FWorldObject* Target = Session.Document.Get().FindObject( Edited.ObjectId );
			const FString Preview = Edited.ObjectId == WorldNoId ? FString( "(none)" ) : Target != nullptr ? ObjectLabel( *Target ) : FString::Printf( "(missing #%d)", Edited.ObjectId );
			if ( Edited.ObjectId != WorldNoId && Target == nullptr )
			{
				ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) );
			}
			const bool bOpen = ImGui::BeginCombo( "##V", UI_TEXT( Preview ) );
			if ( Edited.ObjectId != WorldNoId && Target == nullptr )
			{
				ImGui::PopStyleColor();
			}
			if ( bOpen )
			{
				if ( ImGui::Selectable( "(none)", Edited.ObjectId == WorldNoId ) )
				{
					Edited.ObjectId = WorldNoId;
					bChanged = true;
				}
				for ( const FWorldObject& Other : Session.Document.Get().Objects )
				{
					if ( Other.Id == Object.Id )
					{
						continue;
					}
					if ( ImGui::Selectable( UI_TEXT( ObjectLabel( Other ) ), Other.Id == Edited.ObjectId ) )
					{
						Edited.ObjectId = Other.Id;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			break;
		}
		}
		if ( bChanged )
		{
			NWorldEdit::SetProp( Session.Document, Object.Id, Edited, *Key );
		}
	}

	void DrawPropsSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Properties  (values the code reads from this object)" );
		const TArray<FWorldProp> Props = Object.Props;
		for ( int32 i = 0; i < Props.Num(); ++i )
		{
			ImGui::PushID( i );
			if ( ImGui::SmallButton( "x" ) )
			{
				NWorldEdit::RemoveProp( Session.Document, Object.Id, Props[i].Name );
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::Text( "%s", UI_TEXT( Props[i].Name ) );
			ImGui::SameLine();
			ImGui::TextDisabled( "(%s)", ToString( Props[i].Type ) );
			DrawPropEditor( S, Object, Props[i] );
			ImGui::PopID();
		}
		if ( Props.IsEmpty() )
		{
			ImGui::TextDisabled( "(no properties)" );
		}

		ImGui::SetNextItemWidth( 110.0f );
		NDebugUI::InputStringWithHint( "##NewPropName", "name", S.NewPropName );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::Combo( "##NewPropType", &S.NewPropType, "Bool\0Int\0Float\0String\0Vector3\0Asset\0Object\0" );
		ImGui::SameLine();
		const FString Name = S.NewPropName.TrimStartAndEnd();
		ImGui::BeginDisabled( Name.IsEmpty() || Object.FindProp( Name ) != nullptr );
		if ( ImGui::Button( "Add" ) )
		{
			FWorldProp Prop;
			switch ( static_cast<EWorldPropType>( S.NewPropType ) )
			{
			case EWorldPropType::Bool: Prop = FWorldProp::MakeBool( Name, false ); break;
			case EWorldPropType::Int: Prop = FWorldProp::MakeInt( Name, 0 ); break;
			case EWorldPropType::Float: Prop = FWorldProp::MakeFloat( Name, 0.0 ); break;
			case EWorldPropType::String: Prop = FWorldProp::MakeString( Name, FString() ); break;
			case EWorldPropType::Vector3: Prop = FWorldProp::MakeVector3( Name, FVector3::Zero ); break;
			case EWorldPropType::Asset: Prop = FWorldProp::MakeAsset( Name, FString() ); break;
			case EWorldPropType::Object: Prop = FWorldProp::MakeObject( Name, WorldNoId ); break;
			}
			NWorldEdit::SetProp( Session.Document, Object.Id, Prop );
			S.NewPropName = FString();
		}
		ImGui::EndDisabled();
		if ( Object.FindProp( Name ) != nullptr )
		{
			ImGui::TextDisabled( "A property with that name already exists." );
		}
	}

	void DrawComponents( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		const int32 Id = Object.Id;
		SectionTitle( "Components" );
		bool bRemove = false;

		if ( Object.Model.IsSet() )
		{
			ImGui::PushID( "Model" );
			if ( ComponentHeader( "Model", bRemove ) && !bRemove )
			{
				FWorldModel Model = Object.Model.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Model.Path, S.ModelFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Model.GetValue().Path = Model.Path;
				}
				DrawAssetPathField( S, "Animation set (optional)", Model.Animations, S.AnimationFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Model.GetValue().Animations = Model.Animations;
				}
				if ( NDebugUI::InputStringWithHint( "##Clip", "clip to play (optional)", Model.Clip ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Model.Clip" )->Model.GetValue().Clip = Model.Clip;
				}
				if ( ImGui::DragFloat( "Speed", &Model.Speed, 0.01f, 0.0f, 10.0f ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Model.Speed" )->Model.GetValue().Speed = Model.Speed;
				}
				ImGui::BeginDisabled( !S.AssetInfo.IsValid() || Object.Mesh.IsSet() );
				if ( ImGui::Button( "Convert to editable mesh" ) && S.AssetInfo.IsValid() )
				{
					TWorldDocument::FBatch Batch( Session.Document );
					if ( !NWorldEditor::ConvertModelToMesh( Session.Document, Id, *S.AssetInfo ) )
					{
						NWorldEditorUI::SetStatus( S, "The model's triangles cannot be read (not a mesh model, or not loaded)", true );
					}
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
				{
					ImGui::SetTooltip( "Replace the model by a Mesh component with the same triangles, so that it can be edited (images and colours are not carried over)" );
				}
				NWorldEditorUI::DrawModelMaterialEffectPanel( S, Id );
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Model.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Mesh.IsSet() )
		{
			ImGui::PushID( "Mesh" );
			if ( ComponentHeader( "Mesh", bRemove ) && !bRemove )
			{
				NWorldEditorUI::DrawMeshComponentBody( S, Object );
			}
			if ( bRemove )
			{
				if ( Session.GetMeshObjectId() == Id )
				{
					Session.EndMeshEdit();
				}
				NWorldEdit::EditObject( Session.Document, Id )->Mesh.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Prefab.IsSet() )
		{
			ImGui::PushID( "Prefab" );
			if ( ComponentHeader( "Prefab  (a nested world)", bRemove ) && !bRemove )
			{
				FString Path = Object.Prefab.GetValue().Path;
				bool bChanged = false;
				DrawAssetPathField( S, "World file", Path, S.WorldFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Prefab.GetValue().Path = Path;
				}
				const FWorldDefinition* Contents = S.AssetInfo.IsValid() && !Path.IsEmpty() ? S.AssetInfo->GetWorld( Path ) : nullptr;
				if ( Contents != nullptr )
				{
					ImGui::TextDisabled( "%d object(s) inside", Contents->Objects.Num() );
				}
				else if ( !Path.IsEmpty() )
				{
					ImGui::TextDisabled( "(the contents cannot be read)" );
				}
				if ( !Path.IsEmpty() && NWorld::NormalizeAssetPath( Path ) == NWorld::NormalizeAssetPath( Session.FilePath ) )
				{
					ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "This world places itself" );
				}
				ImGui::BeginDisabled( Path.IsEmpty() );
				if ( ImGui::Button( "Open" ) )
				{
					NWorldEditorUI::RequestOpenWorld( S, Path );
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
				{
					ImGui::SetTooltip( "Edit the nested world itself (asks to save this one first)" );
				}
				ImGui::SameLine();
				ImGui::BeginDisabled( Contents == nullptr || !S.AssetInfo.IsValid() );
				if ( ImGui::Button( "Unpack" ) && S.AssetInfo.IsValid() )
				{
					Session.UnpackPrefabs( TArray<int32>{ Id }, *S.AssetInfo );
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
				{
					ImGui::SetTooltip( "Copy the contents into this world as children of this object (the Prefab component is removed)" );
				}
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Prefab.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Billboard.IsSet() )
		{
			ImGui::PushID( "Billboard" );
			if ( ComponentHeader( "Billboard", bRemove ) && !bRemove )
			{
				FWorldBillboard Board = Object.Billboard.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "Image", Board.Texture, S.TextureFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().Texture = Board.Texture;
				}
				int32 Mode = static_cast<int32>( Board.Mode );
				ImGui::SetNextItemWidth( -FLT_MIN );
				if ( ImGui::Combo( "##Mode", &Mode, BillboardModeNames, 3 ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().Mode = static_cast<EWorldBillboardMode>( Mode );
				}
				if ( ImGui::DragFloat2( "Size", &Board.Size.x, 0.01f, 0.0f, 0.0f, "%.3f" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Size" )->Billboard.GetValue().Size = Board.Size;
				}
				if ( ImGui::DragFloat2( "Pivot", &Board.Pivot.x, 0.005f, 0.0f, 1.0f, "%.3f" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Pivot" )->Billboard.GetValue().Pivot = Board.Pivot;
				}
				if ( ImGui::IsItemHovered() )
				{
					ImGui::SetTooltip( "The point of the image that sits at the object's position (0,0 = top-left, 0.5,1 = bottom-centre)" );
				}
				float32 Tint[4] = { Board.Tint.r / 255.0f, Board.Tint.g / 255.0f, Board.Tint.b / 255.0f, Board.Tint.a / 255.0f };
				if ( ImGui::ColorEdit4( "Tint", Tint ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Tint" )->Billboard.GetValue().Tint = FColor32( static_cast<uint8>( Tint[0] * 255.0f + 0.5f ), static_cast<uint8>( Tint[1] * 255.0f + 0.5f ), static_cast<uint8>( Tint[2] * 255.0f + 0.5f ), static_cast<uint8>( Tint[3] * 255.0f + 0.5f ) );
				}
				if ( ImGui::Checkbox( "Flip X", &Board.bFlipX ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bFlipX = Board.bFlipX;
				}
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Flip Y", &Board.bFlipY ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bFlipY = Board.bFlipY;
				}
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Use part of the image", &Board.bUseRect ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bUseRect = Board.bUseRect;
				}
				if ( Board.bUseRect )
				{
					int32 Rect[4] = { Board.RectX, Board.RectY, Board.RectWidth, Board.RectHeight };
					if ( ImGui::DragInt4( "x y w h", Rect, 1.0f, 0, 16384 ) )
					{
						FWorldBillboard& Target = NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Rect" )->Billboard.GetValue();
						Target.RectX = Rect[0];
						Target.RectY = Rect[1];
						Target.RectWidth = Rect[2];
						Target.RectHeight = Rect[3];
					}
				}
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Billboard.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Sound.IsSet() )
		{
			ImGui::PushID( "Sound" );
			if ( ComponentHeader( "Sound", bRemove ) && !bRemove )
			{
				FWorldSound Sound = Object.Sound.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Sound.Path, S.SoundFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().Path = Sound.Path;
				}
				int32 Bus = 3;
				for ( int32 i = 0; i < 6; ++i )
				{
					if ( Sound.Bus.Equals( BusNames[i], ESearchCase::IgnoreCase ) )
					{
						Bus = i;
					}
				}
				ImGui::SetNextItemWidth( 140.0f );
				if ( ImGui::Combo( "Bus", &Bus, BusNames, 6 ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().Bus = BusNames[Bus];
				}
				if ( ImGui::SliderFloat( "Volume", &Sound.Volume, 0.0f, 2.0f ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Volume" )->Sound.GetValue().Volume = Sound.Volume;
				}
				if ( ImGui::SliderFloat( "Pitch", &Sound.Pitch, 0.25f, 4.0f ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Pitch" )->Sound.GetValue().Pitch = Sound.Pitch;
				}
				if ( ImGui::Checkbox( "Loop", &Sound.bLoop ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().bLoop = Sound.bLoop;
				}
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Play when the scene starts", &Sound.bAutoPlay ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().bAutoPlay = Sound.bAutoPlay;
				}
				float32 Range[2] = { Sound.MinDistance, Sound.MaxDistance };
				if ( ImGui::DragFloat2( "Near / far distance", Range, 0.1f, 0.0f, 10000.0f ) )
				{
					FWorldSound& Target = NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Range" )->Sound.GetValue();
					Target.MinDistance = Range[0];
					Target.MaxDistance = Range[1];
				}
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Sound.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Effect.IsSet() )
		{
			ImGui::PushID( "Effect" );
			if ( ComponentHeader( "Effect", bRemove ) && !bRemove )
			{
				FWorldEffect Effect = Object.Effect.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Effect.Path, S.EffectFiles, bChanged );
				if ( bChanged )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Path = Effect.Path;
				}
				if ( !Effect.Path.IsEmpty() && FEffectBackendRegistry::Global().Find( Effect.Path ) == nullptr )
				{
					ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ), "No effect backend handles this kind of file" );
				}
				if ( ImGui::Checkbox( "Play when the scene starts", &Effect.bAutoPlay ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().bAutoPlay = Effect.bAutoPlay;
				}
				if ( ImGui::DragFloat( "Scale", &Effect.Scale, 0.01f, 0.01f, 100.0f, "%.2f" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Scale" )->Effect.GetValue().Scale = Effect.Scale;
				}
				if ( ImGui::DragFloat( "Speed", &Effect.Speed, 0.01f, 0.0f, 10.0f, "%.2f" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Speed" )->Effect.GetValue().Speed = Effect.Speed;
				}
				if ( ImGui::InputInt( "Seed (0 = the effect's)", &Effect.Seed ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Seed" )->Effect.GetValue().Seed = NMath::Max( Effect.Seed, 0 );
				}
				const char* TargetPreview = Effect.Target == WorldNoId ? "(none)" : "(missing object)";
				if ( const FWorldObject* TargetObject = Session.Document.Get().FindObject( Effect.Target ); TargetObject != nullptr )
				{
					TargetPreview = UI_TEXT( TargetObject->Name );
				}
				if ( ImGui::BeginCombo( "Target object", TargetPreview ) )
				{
					if ( ImGui::Selectable( "(none)", Effect.Target == WorldNoId ) )
					{
						NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Target = WorldNoId;
					}
					for ( const FWorldObject& Candidate : Session.Document.Get().Objects )
					{
						if ( Candidate.Id == Id )
						{
							continue;
						}
						ImGui::PushID( Candidate.Id );
						if ( ImGui::Selectable( UI_TEXT( Candidate.Name ), Effect.Target == Candidate.Id ) )
						{
							NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Target = Candidate.Id;
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				FVector3 TargetOffset = NMath::IsFinite( Effect.TargetOffset.x ) && NMath::IsFinite( Effect.TargetOffset.y ) && NMath::IsFinite( Effect.TargetOffset.z ) ? Effect.TargetOffset : FVector3::Zero;
				if ( ImGui::DragFloat3( "Target offset", &TargetOffset.x, 0.05f, 0.0f, 0.0f, "%.3f" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.TargetOffset" )->Effect.GetValue().TargetOffset = TargetOffset;
				}
				if ( ResetButton( "R##EffectTargetOffset" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().TargetOffset = FVector3::Zero;
				}
				int32 Loop = Effect.Loop + 1;   // 0: エフェクトの指定のまま / 1: 繰り返さない / 2: 繰り返す
				ImGui::SetNextItemWidth( 180.0f );
				if ( ImGui::Combo( "Loop", &Loop, "As the effect says\0Once\0Loop\0" ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Loop = Loop - 1;
				}
				if ( CAssetSubsystem* Assets = NWorldEditorUI::GetAssets(); Assets != nullptr && !Effect.Path.IsEmpty() )
				{
					if ( const TSharedPtr<CEffectAsset> Asset = Assets->LoadEffect( Effect.Path ); Asset.IsValid() )
					{
						const FString Summary = Asset->Describe();
						ImGui::TextDisabled( "%s", UI_TEXT( Summary ) );
					}
				}
				if ( ImGui::Button( "Restart preview" ) )
				{
					++S.EffectRestartSerial;
				}
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Effect.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Collider.IsSet() )
		{
			ImGui::PushID( "Collider" );
			if ( ComponentHeader( "Collider", bRemove ) && !bRemove )
			{
				FWorldCollider Collider = Object.Collider.GetValue();
				int32 Shape = static_cast<int32>( Collider.Shape );
				ImGui::SetNextItemWidth( 140.0f );
				if ( ImGui::Combo( "Shape", &Shape, ShapeNames, 4 ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().Shape = static_cast<EWorldColliderShape>( Shape );
				}

				// モデルの大きさに合わせる（選んでいるすべてのオブジェクトに。Model を持つものだけ）
				ImGui::BeginDisabled( !S.AssetInfo.IsValid() );
				ImGui::TextDisabled( "Fit to the model" );
				for ( const EWorldColliderShape Fit : { EWorldColliderShape::Box, EWorldColliderShape::Sphere, EWorldColliderShape::Capsule } )
				{
					ImGui::SameLine();
					if ( ImGui::SmallButton( ToString( Fit ) ) && S.AssetInfo.IsValid() )
					{
						Session.FitSelectionColliders( Fit, *S.AssetInfo );
					}
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
				{
					ImGui::SetTooltip( "Sets the shape and size from the Model's bounds, for every selected object that has a Model" );
				}

				if ( Collider.Shape == EWorldColliderShape::Mesh )
				{
					bool bChanged = false;
					FString MeshFile = Collider.Mesh;
					DrawAssetPathField( S, "Triangles from (empty = this object's Mesh or Model)", MeshFile, S.ModelFiles, bChanged );
					if ( bChanged )
					{
						NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().Mesh = MeshFile;
					}
					const FString Source = NWorld::GetColliderMeshPath( Object );
					if ( NWorld::UsesEditMeshCollider( Object ) )
					{
						ImGui::TextDisabled( "Using this object's Mesh component (%d triangles)", Object.Mesh.GetValue().GetNumTriangles() );
					}
					else if ( Source.IsEmpty() )
					{
						ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "No triangles: add a Mesh or Model component, or choose a file above" );
					}
					else if ( const FEditorCollisionMesh* Mesh = S.AssetInfo.IsValid() ? S.AssetInfo->GetCollisionMesh( Source ) : nullptr )
					{
						constexpr int32 HeavyTriangles = 30000;
						ImGui::TextDisabled( "%d triangles from '%s'", Mesh->GetNumTriangles(), UI_TEXT( Source ) );
						if ( Mesh->GetNumTriangles() > HeavyTriangles )
						{
							ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.4f, 1.0f ), "Many triangles: slow to build and to simulate. A simpler model for the collision is better." );
						}
					}
					else
					{
						ImGui::TextDisabled( "Triangles of '%s' are not available (not a mesh model, or not loaded)", UI_TEXT( Source ) );
					}
					ImGui::TextWrapped( "Static only (does not move with the object). Uses the model's own coordinates; the object's scale is applied, Offset is not used. One-sided: bodies pass through from behind." );
				}
				else if ( Collider.Shape == EWorldColliderShape::Box )
				{
					if ( ImGui::DragFloat3( "Size", &Collider.Size.x, 0.02f, 0.001f, 10000.0f ) )
					{
						NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Size" )->Collider.GetValue().Size = Collider.Size;
					}
				}
				else
				{
					if ( ImGui::DragFloat( "Radius", &Collider.Radius, 0.01f, 0.001f, 10000.0f ) )
					{
						NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Radius" )->Collider.GetValue().Radius = Collider.Radius;
					}
					if ( Collider.Shape == EWorldColliderShape::Capsule && ImGui::DragFloat( "Height", &Collider.Height, 0.02f, 0.001f, 10000.0f ) )
					{
						NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Height" )->Collider.GetValue().Height = Collider.Height;
					}
				}
				if ( Collider.Shape != EWorldColliderShape::Mesh && ImGui::DragFloat3( "Offset", &Collider.Offset.x, 0.02f ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Offset" )->Collider.GetValue().Offset = Collider.Offset;
				}
				if ( ImGui::Checkbox( "Trigger (reports overlaps, does not block)", &Collider.bTrigger ) )
				{
					NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().bTrigger = Collider.bTrigger;
				}
			}
			if ( bRemove )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Collider.Reset();
			}
			ImGui::PopID();
		}

		for ( const FWorldRawComponent& Raw : Object.RawComponents )
		{
			ImGui::TextDisabled( "%s  (not supported by this editor yet; kept as it is)", UI_TEXT( Raw.Type ) );
		}

		// 足す
		if ( ImGui::Button( "Add component..." ) )
		{
			ImGui::OpenPopup( "##AddComponent" );
		}
		if ( ImGui::BeginPopup( "##AddComponent" ) )
		{
			if ( ImGui::MenuItem( "Model", nullptr, false, !Object.Model.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Model = FWorldModel();
			}
			if ( ImGui::MenuItem( "Mesh (editable)", nullptr, false, !Object.Mesh.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Mesh = NEditMesh::MakePrimitive( EEditMeshPrimitive::Box );
			}
			if ( ImGui::MenuItem( "Prefab (nested world)", nullptr, false, !Object.Prefab.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Prefab = FWorldPrefab();
			}
			if ( ImGui::MenuItem( "Billboard", nullptr, false, !Object.Billboard.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Billboard = FWorldBillboard();
			}
			if ( ImGui::MenuItem( "Sound", nullptr, false, !Object.Sound.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Sound = FWorldSound();
			}
			if ( ImGui::MenuItem( "Effect (particles)", nullptr, false, !Object.Effect.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Effect = FWorldEffect();
			}
			if ( ImGui::MenuItem( "Collider", nullptr, false, !Object.Collider.IsSet() ) )
			{
				NWorldEdit::EditObject( Session.Document, Id )->Collider = FWorldCollider();
			}
			ImGui::EndPopup();
		}
	}

	void DrawCollisionSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Collision" );
		int32 Mode = static_cast<int32>( Object.Collision );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( ImGui::Combo( "##CollisionMode", &Mode, CollisionModeNames, 3 ) )
		{
			NWorldEdit::EditObject( Session.Document, Object.Id )->Collision = static_cast<EWorldCollisionMode>( Mode );
		}

		const FWorldCollisionDecision Decision = NWorld::ResolveCollision( Session.Document.Get().Settings, Object );
		const FString Text = Decision.ToDisplayString();
		ImGui::TextColored( Decision.bEnabled ? ImVec4( 0.5f, 0.9f, 0.55f, 1.0f ) : ImVec4( 0.85f, 0.6f, 0.5f, 1.0f ), "%s", UI_TEXT( Text ) );
		if ( !Object.Collider.IsSet() )
		{
			ImGui::TextDisabled( "Add a Collider component to give this object a collision shape." );
		}
	}
}

void NWorldEditorUI::DrawInspector( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Inspector", &S.bShowInspector, 0.60f, 0.38f, 0.39f, 0.60f ) )
	{
		return;
	}
	CWorldEditorSession& Session = S.Session;
	const FWorldObject* Object = Session.Document.Get().FindObject( Session.GetPrimary() );
	if ( Object == nullptr )
	{
		ImGui::TextDisabled( "Nothing selected." );
		ImGui::TextWrapped( "Click an object in the view or the outliner. Drag a model, image or sound from World Assets into the view to place it." );
		ImGui::End();
		return;
	}
	const int32 Id = Object->Id;
	if ( Session.GetSelection().Num() > 1 )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.4f, 1.0f ), "%d objects selected (editing the last one)", Session.GetSelection().Num() );
	}

	// 名前・有効
	ImGui::TextDisabled( "Id %d", Id );
	ImGui::SameLine();
	bool bEnabled = Object->bEnabled;
	if ( ImGui::Checkbox( "Enabled", &bEnabled ) )
	{
		NWorldEdit::EditObject( Session.Document, Id )->bEnabled = bEnabled;
	}
	FString Name = Object->Name;
	ImGui::SetNextItemWidth( -FLT_MIN );
	if ( NDebugUI::InputStringWithHint( "##Name", "name", Name ) )
	{
		NWorldEdit::EditObject( Session.Document, Id, "Inspector.Name" )->Name = Name;
	}

	// 親
	if ( Object->Parent != WorldNoId )
	{
		const FWorldObject* Parent = Session.Document.Get().FindObject( Object->Parent );
		ImGui::TextDisabled( "Parent  %s", Parent != nullptr ? UI_TEXT( ObjectLabel( *Parent ) ) : "(missing)" );
	}

	DrawTransformSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawTagsSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawPropsSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawComponents( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawCollisionSection( S, *Object );

	// この物の検証の問題
	bool bShownHeader = false;
	for ( const FWorldIssue& Issue : S.Issues )
	{
		if ( Issue.ObjectId != Id )
		{
			continue;
		}
		if ( !bShownHeader )
		{
			SectionTitle( "Problems" );
			bShownHeader = true;
		}
		ImGui::TextColored( Issue.Severity == EWorldIssueSeverity::Error ? ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) : ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ), "%s", UI_TEXT( Issue.Message ) );
	}
	if ( Object != nullptr && !Object->Extra.is_null() && !Object->Extra.empty() )
	{
		ImGui::TextDisabled( "Also holds %d unknown field(s) from another editor / version (kept as they are).", static_cast<int32>( Object->Extra.size() ) );
	}
	ImGui::End();
}
