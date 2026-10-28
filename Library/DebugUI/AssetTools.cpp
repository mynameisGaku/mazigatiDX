#include "AssetTools.h"


#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Platform/CommandLine.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	const char* TypeLabels[] = { "All", "Texture", "SpriteSheet", "Model", "Sound", "Font", "ModelAnimation", "World", "Effect" };

	ImVec4 TypeColor( EAssetType Type )
	{
		switch ( Type )
		{
		case EAssetType::Texture: return ImVec4( 0.45f, 0.75f, 1.0f, 1.0f );
		case EAssetType::SpriteSheet: return ImVec4( 0.55f, 0.95f, 0.75f, 1.0f );
		case EAssetType::Model: return ImVec4( 1.0f, 0.75f, 0.45f, 1.0f );
		case EAssetType::Sound: return ImVec4( 0.95f, 0.6f, 0.95f, 1.0f );
		case EAssetType::Font: return ImVec4( 0.95f, 0.95f, 0.55f, 1.0f );
		case EAssetType::ModelAnimation: return ImVec4( 1.0f, 0.6f, 0.5f, 1.0f );
		case EAssetType::World: return ImVec4( 0.7f, 0.85f, 1.0f, 1.0f );
		case EAssetType::Effect: return ImVec4( 1.0f, 0.85f, 0.4f, 1.0f );
		}
		return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	//=========================================================================
	// Assets
	//=========================================================================
	void DrawAssetWindow( FAssetToolsState& State, bool* pOpen )
	{
		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 20.0f, MainViewport->Pos.y + 44.0f ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( ImVec2( NMath::Min( 780.0f, MainViewport->Size.x - 40.0f ), NMath::Min( 560.0f, MainViewport->Size.y - 60.0f ) ), ImGuiCond_FirstUseEver );
		if ( !ImGui::Begin( "Assets", pOpen ) )
		{
			ImGui::End();
			return;
		}
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		if ( Assets == nullptr )
		{
			ImGui::TextUnformatted( "The asset manager is not running." );
			ImGui::End();
			return;
		}
		const FAssetStats& Stats = Assets->GetStats();
		const auto Bytes = []( int64 Value ) { return NDebugUI::FormatBytes( Value ); };

		// 全体
		ImGui::Text( "Root folder: %s", UI_TEXT( Assets->GetRootDirectory() ) );
		ImGui::Text( "%d asset(s) loaded, ~%s in memory (peak ~%s), %s on disk", Stats.TotalCount, UI_TEXT( Bytes( Stats.TotalMemoryBytes ) ), UI_TEXT( Bytes( Stats.PeakMemoryBytes ) ),
					 UI_TEXT( Bytes( Stats.TotalFileBytes ) ) );
		ImGui::Text( "Loads: %d (%.1f ms total, slowest %.1f ms), %d shared from the cache, %d failed, %d loading now", Stats.NumLoads, Stats.TotalLoadMs, Stats.MaxLoadMs, Stats.NumCacheHits,
					 Stats.NumLoadFailures, Stats.NumLoading );
		if ( !Stats.MaxLoadPath.IsEmpty() ) ImGui::TextDisabled( "Slowest: %s", UI_TEXT( Stats.MaxLoadPath ) );
		if ( Stats.Backend.VideoMemoryTotal > 0 )
		{
			const float Fraction = static_cast<float>( static_cast<double>( Stats.Backend.VideoMemoryUsed ) / static_cast<double>( Stats.Backend.VideoMemoryTotal ) );
			char Overlay[96];
			NCString::Format( Overlay, sizeof( Overlay ), "Video memory %s / %s", UI_TEXT( Bytes( Stats.Backend.VideoMemoryUsed ) ), UI_TEXT( Bytes( Stats.Backend.VideoMemoryTotal ) ) );
			ImGui::ProgressBar( Fraction, ImVec2( 360.0f, 0.0f ), Overlay );
			ImGui::SameLine();
		}
		ImGui::Text( "Process memory %s   handles: %d graph, %d model, %d sound, %d font", UI_TEXT( Bytes( Stats.Backend.ProcessWorkingSet ) ), Stats.Backend.NumTextureHandles,
					 Stats.Backend.NumModelHandles, Stats.Backend.NumSoundHandles, Stats.Backend.NumFontHandles );

		// 種類ごと
		if ( ImGui::BeginTable( "##Types", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit ) )
		{
			ImGui::TableSetupColumn( "Type" );
			ImGui::TableSetupColumn( "Count" );
			ImGui::TableSetupColumn( "Memory (est.)" );
			ImGui::TableSetupColumn( "File size" );
			ImGui::TableSetupColumn( "Load time" );
			ImGui::TableHeadersRow();
			for ( int32 i = 0; i < NumAssetTypes; ++i )
			{
				const FAssetTypeStats& Type = Stats.Types[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored( TypeColor( static_cast<EAssetType>( i ) ), "%s", ToString( static_cast<EAssetType>( i ) ) );
				ImGui::TableNextColumn();
				ImGui::Text( "%d%s", Type.Count, Type.NumNotReady > 0 ? " (loading)" : "" );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UI_TEXT( Bytes( Type.MemoryBytes ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UI_TEXT( Bytes( Type.FileBytes ) ) );
				ImGui::TableNextColumn();
				ImGui::Text( "%.1f ms", Type.LoadMs );
			}
			ImGui::EndTable();
		}

		if ( ImGui::Button( "Release unused" ) ) Assets->ReleaseUnused();
		ImGui::SameLine();
		if ( ImGui::Button( "Print to log" ) ) Assets->DumpToLog();
		ImGui::SameLine();
		if ( ImGui::Button( "Export models to .mv1" ) )
		{
			const FModelBatchExportResult Batch = Assets->ExportAllModels( false );
			State.ExportStatusMessage = ".mv1 export: " + Batch.GetSummary();
			State.bExportStatusError = Batch.NumFailed > 0;
		}
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Convert any new or modified .fbx / .obj / .x / .pmx / .vmd under the asset root to .mv1." );
		ImGui::SameLine();
		if ( ImGui::Button( "Rebuild all .mv1" ) )
		{
			const FModelBatchExportResult Batch = Assets->ExportAllModels( true );
			State.ExportStatusMessage = ".mv1 rebuild: " + Batch.GetSummary();
			State.bExportStatusError = Batch.NumFailed > 0;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 130.0f );
		ImGui::Combo( "##TypeFilter", &State.AssetTypeFilter, "All\0Texture\0SpriteSheet\0Model\0Sound\0Font\0ModelAnimation\0World\0Effect\0" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 220.0f );
		ImGui::InputTextWithHint( "##AssetFilter", "filter (path / description)", State.AssetFilter, sizeof( State.AssetFilter ) );
		(void)TypeLabels;
		if ( !State.ExportStatusMessage.IsEmpty() )
		{
			if ( State.bExportStatusError ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "%s", UI_TEXT( State.ExportStatusMessage ) );
			else ImGui::TextDisabled( "%s", UI_TEXT( State.ExportStatusMessage ) );
		}

		TArray<FAssetInfo> Infos = Assets->GetLoadedAssets();
		const FString Filter = State.AssetFilter;
		Infos.RemoveAll( [&]( const FAssetInfo& Info )
		{
			if ( State.AssetTypeFilter > 0 && static_cast<int32>( Info.Type ) != State.AssetTypeFilter - 1 ) return true;
			if ( !Filter.IsEmpty() && !Info.Path.Contains( Filter, ESearchCase::IgnoreCase ) && !Info.Description.Contains( Filter, ESearchCase::IgnoreCase ) ) return true;
			return false;
		} );

		enum EColumn { ColType, ColPath, ColRefs, ColMemory, ColFile, ColLoad, ColInfo };
		if ( ImGui::BeginTable( "##Assets", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp ) )
		{
			ImGui::TableSetupScrollFreeze( 0, 1 );
			ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_PreferSortAscending, 1.0f, ColType );
			ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_PreferSortAscending, 4.0f, ColPath );
			ImGui::TableSetupColumn( "Refs", ImGuiTableColumnFlags_PreferSortDescending, 0.5f, ColRefs );
			ImGui::TableSetupColumn( "Memory", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.0f, ColMemory );
			ImGui::TableSetupColumn( "File", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, ColFile );
			ImGui::TableSetupColumn( "Load ms", ImGuiTableColumnFlags_PreferSortDescending, 0.8f, ColLoad );
			ImGui::TableSetupColumn( "Info", 0, 2.5f, ColInfo );
			ImGui::TableHeadersRow();
			if ( ImGuiTableSortSpecs* Specs = ImGui::TableGetSortSpecs() )
			{
				if ( Specs->SpecsCount > 0 )
				{
					State.AssetSortColumn = static_cast<int32>( Specs->Specs[0].ColumnUserID );
					State.bAssetSortAscending = Specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
				}
			}
			const int32 Column = State.AssetSortColumn;
			const bool bAscending = State.bAssetSortAscending;
			Infos.StableSort( [Column, bAscending]( const FAssetInfo& A, const FAssetInfo& B )
			{
				float64 ValueA = 0.0, ValueB = 0.0;
				switch ( Column )
				{
				case ColType: ValueA = static_cast<float64>( A.Type ); ValueB = static_cast<float64>( B.Type ); break;
				case ColPath: { const int32 Compare = A.Path.Compare( B.Path, ESearchCase::IgnoreCase ); return bAscending ? Compare < 0 : Compare > 0; }
				case ColRefs: ValueA = A.RefCount; ValueB = B.RefCount; break;
				case ColFile: ValueA = static_cast<float64>( A.FileBytes ); ValueB = static_cast<float64>( B.FileBytes ); break;
				case ColLoad: ValueA = A.LoadMs; ValueB = B.LoadMs; break;
				case ColInfo: { const int32 Compare = A.Description.Compare( B.Description, ESearchCase::IgnoreCase ); return bAscending ? Compare < 0 : Compare > 0; }
				case ColMemory:
				default: ValueA = static_cast<float64>( A.MemoryBytes ); ValueB = static_cast<float64>( B.MemoryBytes ); break;
				}
				return bAscending ? ValueA < ValueB : ValueA > ValueB;
			} );

			ImGuiListClipper Clipper;
			Clipper.Begin( Infos.Num() );
			while ( Clipper.Step() )
			{
				for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
				{
					const FAssetInfo& Info = Infos[i];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored( TypeColor( Info.Type ), "%s", ToString( Info.Type ) );
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( UI_TEXT( Info.Path ) );
					if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", UI_TEXT( Info.Path ) );
					ImGui::TableNextColumn();
					ImGui::Text( "%d", Info.RefCount );
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( UI_TEXT( Bytes( Info.MemoryBytes ) ) );
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( UI_TEXT( Bytes( Info.FileBytes ) ) );
					ImGui::TableNextColumn();
					ImGui::Text( "%.2f", Info.LoadMs );
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( Info.bReady ? UI_TEXT( Info.Description ) : "loading..." );
				}
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}

	//=========================================================================
	// Audio
	//=========================================================================
	void DrawAudioWindow( FAssetToolsState&, bool* pOpen )
	{
		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		const float AudioWidth = NMath::Min( 450.0f, MainViewport->Size.x - 40.0f );
		ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + NMath::Max( 20.0f, MainViewport->Size.x - AudioWidth - 20.0f ), MainViewport->Pos.y + 44.0f ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( ImVec2( AudioWidth, NMath::Min( 500.0f, MainViewport->Size.y - 60.0f ) ), ImGuiCond_FirstUseEver );
		if ( !ImGui::Begin( "Audio", pOpen ) )
		{
			ImGui::End();
			return;
		}
		CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
		if ( Audio == nullptr )
		{
			ImGui::TextUnformatted( "The audio system is not running." );
			ImGui::End();
			return;
		}

		ImGui::Text( "%d voice(s) playing%s", Audio->GetNumVoices(), Audio->IsPaused() ? "   (PAUSED)" : "" );
		ImGui::SameLine();
		bool bPaused = Audio->IsPaused();
		if ( ImGui::Checkbox( "Pause all", &bPaused ) ) Audio->SetPaused( bPaused );
		ImGui::SameLine();
		if ( ImGui::Button( "Stop all" ) ) Audio->StopAll( 0.2f );

		// バス
		ImGui::SeparatorText( "Buses" );
		for ( int32 i = 0; i < static_cast<int32>( EAudioBus::Count ); ++i )
		{
			const EAudioBus Bus = static_cast<EAudioBus>( i );
			ImGui::PushID( i );
			ImGui::SetNextItemWidth( 70.0f );
			ImGui::TextUnformatted( ToString( Bus ) );
			ImGui::SameLine( 80.0f );
			float32 Volume = Audio->GetBusVolume( Bus );
			ImGui::SetNextItemWidth( 220.0f );
			if ( ImGui::SliderFloat( "##Volume", &Volume, 0.0f, 1.5f, "%.2f" ) ) Audio->SetBusVolume( Bus, Volume );
			ImGui::SameLine();
			bool bMuted = Audio->IsBusMuted( Bus );
			if ( ImGui::Checkbox( "Mute", &bMuted ) ) Audio->SetBusMuted( Bus, bMuted );
			ImGui::PopID();
		}

		// リスナー
		const FAudioListener& Listener = Audio->GetListener();
		ImGui::TextDisabled( "Listener at (%.1f, %.1f, %.1f) facing (%.2f, %.2f, %.2f)", Listener.Position.x, Listener.Position.y, Listener.Position.z, Listener.Forward.x, Listener.Forward.y, Listener.Forward.z );

		// 鳴っている音
		ImGui::SeparatorText( "Voices" );
		const TArray<FVoiceInfo> Voices = Audio->GetVoiceInfos();
		if ( ImGui::BeginTable( "##Voices", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp ) )
		{
			ImGui::TableSetupScrollFreeze( 0, 1 );
			ImGui::TableSetupColumn( "Sound", 0, 4.0f );
			ImGui::TableSetupColumn( "Bus", 0, 1.0f );
			ImGui::TableSetupColumn( "Volume", 0, 2.2f );
			ImGui::TableSetupColumn( "Pan", 0, 0.8f );
			ImGui::TableSetupColumn( "Pitch", 0, 0.8f );
			ImGui::TableSetupColumn( "Age", 0, 0.8f );
			ImGui::TableSetupColumn( "Flags", 0, 1.6f );
			ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 40.0f );
			ImGui::TableHeadersRow();
			for ( const FVoiceInfo& Voice : Voices )
			{
				ImGui::PushID( static_cast<int>( Voice.Handle.Id ) );
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UI_TEXT( Voice.SoundPath ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( ToString( Voice.Bus ) );
				ImGui::TableNextColumn();
				ImGui::ProgressBar( NMath::Min( Voice.Volume, 1.0f ), ImVec2( -FLT_MIN, 0.0f ), "" );
				ImGui::SameLine( 4.0f );
				ImGui::Text( "%.2f", Voice.Volume );
				ImGui::TableNextColumn();
				ImGui::Text( "%+.2f", Voice.Pan );
				ImGui::TableNextColumn();
				ImGui::Text( "%.2f", Voice.Pitch );
				ImGui::TableNextColumn();
				ImGui::Text( "%.1f s", Voice.Age );
				ImGui::TableNextColumn();
				ImGui::Text( "%s%s%s%s", Voice.bLooping ? "loop " : "", Voice.bPaused ? "paused " : "", Voice.bFading ? "fading " : "", Voice.bSpatial ? "3D" : "" );
				ImGui::TableNextColumn();
				if ( ImGui::SmallButton( "Stop" ) ) Audio->Stop( Voice.Handle, 0.1f );
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}

	bool ContainsToken( const char* List, const char* Token )
	{
		if ( List == nullptr || Token == nullptr ) return false;
		const size_t Length = NCString::Length( Token );
		for ( const char* p = List; *p != '\0'; )
		{
			const char* End = NCString::FindChar( p, ',' );
			const size_t ItemLength = End != nullptr ? static_cast<size_t>( End - p ) : NCString::Length( p );
			if ( ItemLength == Length && NCString::CompareN( p, Token, Length ) == 0 ) return true;
			if ( End == nullptr ) break;
			p = End + 1;
		}
		return false;
	}

	// 起動引数 --Name=値 の値（無ければ空）
	FString GetArgument( const char* Name )
	{
		return NCommandLine::GetValue( Name ).Get( FString() );
	}
}

void NDebugUI::DrawAssetToolsButtons( FAssetToolsState& State )
{
	ImGui::TextUnformatted( "Asset tools:" );
	ImGui::SameLine();
	ImGui::Checkbox( "Assets", &State.bShowAssets );
	ImGui::SameLine();
	ImGui::Checkbox( "Audio", &State.bShowAudio );
	ImGui::SameLine();
	ImGui::Checkbox( "Sprite sheet editor", &State.bShowSpriteSheetEditor );
	ImGui::SameLine();
	ImGui::Checkbox( "Model animation editor", &State.bShowModelAnimationEditor );
	ImGui::SameLine();
	ImGui::Checkbox( "World editor", &State.bShowWorldEditor );
	// 外側の表示切替から閉じるときも、未保存の確認を省略しない。
	bool bShowEffect = State.bShowEffectEditor;
	if ( ImGui::Checkbox( "Effect editor", &bShowEffect ) )
	{
		if ( bShowEffect ) State.bShowEffectEditor = true;
		else NEffectEditorUI::RequestAction( State.EffectEditor, FEffectEditorState::EPending::Close, FString(), &State.bShowEffectEditor );
	}
}

void NDebugUI::DrawAssetTools( FAssetToolsState& State )
{
	if ( !State.bCommandLineApplied )
	{
		State.bCommandLineApplied = true;
		ApplyAssetToolsCommandLine( State );
	}
	NDebugUI::TickImGuiTextureCache();

	if ( State.bShowSpriteSheetEditor ) DrawSpriteSheetEditor( State.SpriteSheet, &State.bShowSpriteSheetEditor );
	if ( State.bShowModelAnimationEditor ) DrawModelAnimationEditor( State.ModelAnimation, &State.bShowModelAnimationEditor );
	if ( State.bShowEffectEditor ) DrawEffectEditor( State.EffectEditor, &State.bShowEffectEditor );
	// 画面を閉じた後や別の窓口から保存した場合も、更新を一度だけ伝える。
	if ( State.ObservedEffectSaveSerial != State.EffectEditor.SaveSerial )
	{
		State.ObservedEffectSaveSerial = State.EffectEditor.SaveSerial;
		++State.WorldEditor.EffectRestartSerial;
		State.WorldEditor.EffectPreviews.Empty();
	}
	if ( State.bShowWorldEditor ) DrawWorldEditor( State.WorldEditor, &State.bShowWorldEditor );
	if ( State.bShowAssets ) DrawAssetWindow( State, &State.bShowAssets );
	if ( State.bShowAudio ) DrawAudioWindow( State, &State.bShowAudio );
}

void NDebugUI::ApplyAssetToolsCommandLine( FAssetToolsState& State )
{
	const FString Tools = GetArgument( "tool" );
	if ( !Tools.IsEmpty() )
	{
		State.bShowAssets = ContainsToken( *Tools, "assets" );
		State.bShowAudio = ContainsToken( *Tools, "audio" );
		State.bShowSpriteSheetEditor = ContainsToken( *Tools, "sprite" );
		State.bShowModelAnimationEditor = ContainsToken( *Tools, "model" );
		State.bShowWorldEditor = ContainsToken( *Tools, "world" );
		State.bShowEffectEditor = ContainsToken( *Tools, "effect" );
	}
	const FString World = GetArgument( "world" );
	if ( !World.IsEmpty() )
	{
		State.bShowWorldEditor = true;
		OpenWorldFile( State.WorldEditor, World );
	}
	// ワールドエディタの最初の状態（スクリーンショットでの確認用）
	const auto SplitNumbers = []( const FString& Text )
	{
		TArray<float32> Numbers;
		FString Rest = Text;
		FString Left, Right;
		while ( Rest.Split( ",", &Left, &Right ) )
		{
			Numbers.Add( Left.ToFloat() );
			Rest = Right;
		}
		if ( !Rest.IsEmpty() ) Numbers.Add( Rest.ToFloat() );
		return Numbers;
	};
	const FString WorldSelect = GetArgument( "world-select" );
	if ( !WorldSelect.IsEmpty() )
	{
		TArray<int32> Ids;
		for ( const float32 Id : SplitNumbers( WorldSelect ) ) Ids.Add( static_cast<int32>( Id ) );
		State.WorldEditor.Session.SetSelection( Ids );
	}
	// 編集メッシュの編集を始める: --world-edit-mesh=要素の種類（vertex / edge / face）。選んだオブジェクト（--world-select）の Mesh 部品を編集する。
	// --world-mesh-select=all で、要素をすべて選ぶ（確認用）
	const FString WorldEditMesh = GetArgument( "world-edit-mesh" );
	if ( !WorldEditMesh.IsEmpty() && State.WorldEditor.Session.BeginMeshEdit( State.WorldEditor.Session.GetPrimary() ) )
	{
		State.WorldEditor.Session.SetMeshElement( WorldEditMesh == "face" ? NEditMesh::EEditMeshElement::Face : WorldEditMesh == "edge" ? NEditMesh::EEditMeshElement::Edge : NEditMesh::EEditMeshElement::Vertex );
		if ( GetArgument( "world-mesh-select" ) == "all" ) State.WorldEditor.Session.MeshSelectAll();
	}
	const FString WorldMode = GetArgument( "world-mode" );
	if ( WorldMode == "rotate" ) State.WorldEditor.Session.GizmoMode = EEditorGizmoMode::Rotate;
	else if ( WorldMode == "scale" ) State.WorldEditor.Session.GizmoMode = EEditorGizmoMode::Scale;
	const FString WorldColliders = GetArgument( "world-colliders" );
	if ( WorldColliders == "all" ) State.WorldEditor.ColliderView = FWorldEditorState::EColliderView::All;
	else if ( WorldColliders == "off" ) State.WorldEditor.ColliderView = FWorldEditorState::EColliderView::Off;
	const FString WorldCamera = GetArgument( "world-camera" );   // yaw,pitch,distance[,x,y,z]
	if ( !WorldCamera.IsEmpty() )
	{
		const TArray<float32> V = SplitNumbers( WorldCamera );
		FEditorCamera& Camera = State.WorldEditor.Session.Camera;
		if ( V.Num() >= 3 )
		{
			Camera.Yaw = V[0];
			Camera.Pitch = V[1];
			Camera.Distance = V[2];
		}
		if ( V.Num() >= 6 ) Camera.Target = FVector3( V[3], V[4], V[5] );
	}
	// 定義ファイルだけを指定した場合も、対応する編集画面を開く。
	const FString Effect = GetArgument( "effect" );
	if ( !Effect.IsEmpty() )
	{
		State.bShowEffectEditor = true;
		State.EffectEditor.bInitialized = true;
		OpenEffectFile( State.EffectEditor, Effect );
	}
	// 原本を保ったコピーから始める起動経路。移動する帯の画像確認にも使う。
	const FString EffectPreset = GetArgument( "effect-preset" );
	if ( !EffectPreset.IsEmpty() )
	{
		State.bShowEffectEditor = true;
		NewEffectFromPreset( State.EffectEditor, EffectPreset );
	}
	const FString Sprite = GetArgument( "sprite" );
	if ( !Sprite.IsEmpty() )
	{
		State.bShowSpriteSheetEditor = true;
		OpenSpriteSheetFile( State.SpriteSheet, Sprite );
	}
	const FString SpriteTab = GetArgument( "sprite-tab" );
	if ( !SpriteTab.IsEmpty() ) State.SpriteSheet.ForceLeftTab = SpriteTab.ToInt();
	const FString ModelTab = GetArgument( "model-tab" );
	if ( !ModelTab.IsEmpty() ) State.ModelAnimation.ForceLeftTab = ModelTab.ToInt();
	const FString ModelAnim = GetArgument( "modelanim" );
	if ( !ModelAnim.IsEmpty() )
	{
		State.bShowModelAnimationEditor = true;
		OpenModelAnimationFile( State.ModelAnimation, ModelAnim );
	}
}

void NDebugUI::ShutdownAssetTools( FAssetToolsState& State )
{
	ReleaseEffectEditor( State.EffectEditor );
	ReleaseModelAnimationEditor( State.ModelAnimation );
	ReleaseWorldEditor( State.WorldEditor );
	State.SpriteSheet.Sheet.Reset();
	State.SpriteSheet.Preview = FSpriteAnimator();
	NDebugUI::ClearImGuiTextureCache();
}
