#include "ModelAnimationEditor.h"


#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelAnimationEditor, Log )

namespace
{
	using FState = FModelAnimationEditorState;

	constexpr const char* DefinitionExtension = ".modelanim.json";

	CAssetManager* GetAssets() { return GetAppSubsystem<CAssetManager>(); }

	void SetStatus( FState& S, const FString& Text, bool bError = false )
	{
		S.StatusMessage = Text;
		S.bStatusIsError = bError;
		if ( bError ) LOG( LogModelAnimationEditor, Warning, "%s", *Text );
	}

	FString NormalizePath( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	void RescanFiles( FState& S, bool bForce )
	{
		CAssetManager* Assets = GetAssets();
		if ( Assets == nullptr ) return;
		const double Now = ImGui::GetTime();
		if ( !bForce && Now - S.LastScanTime < 3.0 ) return;
		S.LastScanTime = Now;

		const FString Root = Assets->GetRootDirectory();
		S.ModelFiles = NFileSystem::ListFilesRecursive( Root, NModelFormat::GetAllModelExtensions() );
		S.DefinitionFiles = NFileSystem::ListFilesRecursive( Root, { DefinitionExtension } );
	}

	const FModelClipDef* SelectedClip( const FState& S )
	{
		return S.Document.Get().Clips.IsValidIndex( S.SelectedClip ) ? &S.Document.Get().Clips[S.SelectedClip] : nullptr;
	}

	TSharedPtr<CModel> GetEffectiveMeshModel( const FState& S )
	{
		if ( S.Asset.IsValid() && S.Asset->GetModel().IsValid() && S.Asset->GetModel()->HasMesh() ) return S.Asset->GetModel();
		if ( S.PreviewModel.IsValid() && S.PreviewModel->HasMesh() ) return S.PreviewModel;
		return TSharedPtr<CModel>();
	}

	//=========================================================================
	// 定義 -> 資産 -> アニメーター
	//=========================================================================
	void ComputeBounds( FState& S )
	{
		S.bHaveBounds = false;
		if ( !S.Animator.Instance.IsValid() ) return;
		const int32 Handle = S.Animator.Instance->GetHandle();
		const int32 NumMeshes = MV1GetMeshNum( Handle );
		FVector3 Min( 1.0e30f, 1.0e30f, 1.0e30f ), Max( -1.0e30f, -1.0e30f, -1.0e30f );
		for ( int32 i = 0; i < NumMeshes; ++i )
		{
			const VECTOR A = MV1GetMeshMinPosition( Handle, i );
			const VECTOR B = MV1GetMeshMaxPosition( Handle, i );
			Min = FVector3( NMath::Min( Min.x, A.x ), NMath::Min( Min.y, A.y ), NMath::Min( Min.z, A.z ) );
			Max = FVector3( NMath::Max( Max.x, B.x ), NMath::Max( Max.y, B.y ), NMath::Max( Max.z, B.z ) );
		}
		if ( Min.x > Max.x ) return;
		S.BoundsMin = Min;
		S.BoundsMax = Max;
		S.bHaveBounds = true;
	}

	void EnsureScreen( FState& S, int32 Width, int32 Height )
	{
		if ( S.PreviewScreen >= 0 && S.PreviewWidth == Width && S.PreviewHeight == Height ) return;
		if ( S.PreviewScreen >= 0 ) DeleteGraph( S.PreviewScreen );
		S.PreviewScreen = MakeScreen( Width, Height, FALSE );
		S.PreviewWidth = Width;
		S.PreviewHeight = Height;
	}

	// 選んだクリップを頭から終わりまで少しずつ動かして、モデルの各フレーム（ボーン）がどこを通るかを調べる。
	// 画面は使わない（描画の実行でアニメーションの時間が入る。座標だけ読む）
	bool SampleMotionRange( FState& S, FVector3& OutMin, FVector3& OutMax )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr || !S.Animator.Instance.IsValid() || S.PreviewScreen < 0 || SelectedClip( S ) == nullptr ) return false;

		const int32 Handle = S.Animator.Instance->GetHandle();
		const int32 NumFrames = MV1GetFrameNum( Handle );
		if ( NumFrames <= 0 ) return false;

		const float32 Saved = S.Animator.GetNormalizedTime();
		OutMin = FVector3( 1.0e30f, 1.0e30f, 1.0e30f );
		OutMax = FVector3( -1.0e30f, -1.0e30f, -1.0e30f );
		SetDrawScreen( S.PreviewScreen );
		constexpr int32 NumSamples = 24;
		for ( int32 i = 0; i <= NumSamples; ++i )
		{
			S.Animator.Seek( static_cast<float32>( i ) / static_cast<float32>( NumSamples ) );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			S.Animator.Record( Draw, FMatrix4x4::Identity );
			Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
			for ( int32 Frame = 0; Frame < NumFrames; ++Frame )
			{
				const VECTOR Position = MV1GetFramePosition( Handle, Frame );
				OutMin = FVector3( NMath::Min( OutMin.x, Position.x ), NMath::Min( OutMin.y, Position.y ), NMath::Min( OutMin.z, Position.z ) );
				OutMax = FVector3( NMath::Max( OutMax.x, Position.x ), NMath::Max( OutMax.y, Position.y ), NMath::Max( OutMax.z, Position.z ) );
			}
		}
		SetDrawScreen( DX_SCREEN_BACK );
		S.Animator.Seek( Saved );
		return OutMin.x <= OutMax.x;
	}

	void FitCamera( FState& S )
	{
		S.bFitRequested = false;
		if ( !S.bHaveBounds )
		{
			S.Target = FVector3::Zero;
			S.Distance = 12.0f;
			return;
		}
		// 見た目の補正（拡大）も含めた、モデル自身の大きさ
		const FModelAnimationDefinition& Definition = S.Document.Get();
		const FVector3 Scale( NMath::Abs( Definition.Scale.x ), NMath::Abs( Definition.Scale.y ), NMath::Abs( Definition.Scale.z ) );
		const FVector3 Center = ( S.BoundsMin + S.BoundsMax ) * 0.5f;
		const FVector3 Extent = ( S.BoundsMax - S.BoundsMin ) * 0.5f;
		float32 Radius = NMath::Max( NMath::Sqrt( Extent.x * Scale.x * Extent.x * Scale.x + Extent.y * Scale.y * Extent.y * Scale.y + Extent.z * Scale.z * Extent.z * Scale.z ), 0.01f );
		FVector3 Middle = FVector3( Center.x * Scale.x, Center.y * Scale.y, Center.z * Scale.z ) + Definition.Offset;

		// 動き回るアニメーション（歩いて前に進む、など）でも画面に収まるように、通る範囲まで含める
		FVector3 MotionMin, MotionMax;
		if ( SampleMotionRange( S, MotionMin, MotionMax ) )
		{
			const FVector3 MotionCenter = ( MotionMin + MotionMax ) * 0.5f;
			const FVector3 MotionExtent = ( MotionMax - MotionMin ) * 0.5f;
			const float32 MotionRadius = NMath::Sqrt( MotionExtent.x * MotionExtent.x + MotionExtent.y * MotionExtent.y + MotionExtent.z * MotionExtent.z );
			if ( MotionRadius > Radius * 0.05f )
			{
				Middle = MotionCenter;
				Radius += MotionRadius;
			}
		}
		S.Target = Middle;
		S.Distance = Radius * 2.6f;
	}

	void PlaySelected( FState& S, bool bKeepTime )
	{
		const float32 Normalized = bKeepTime ? S.Animator.GetNormalizedTime() : 0.0f;
		S.PlayingClip = S.SelectedClip;
		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip == nullptr || !S.Animator.HasClip( Clip->Name ) )
		{
			S.Animator.Stop( 0.0f );
			return;
		}
		FModelPlayParams Params;
		Params.FadeTime = 0.0f;
		Params.bRestartIfPlaying = true;
		if ( !S.Animator.Play( Clip->Name, Params ) ) return;
		if ( bKeepTime ) S.Animator.Seek( Normalized );
		if ( S.bPlaying ) S.Animator.Resume();
		else S.Animator.Pause();
	}

	void SyncPreview( FState& S )
	{
		CAssetManager* Assets = GetAssets();
		if ( Assets == nullptr ) return;
		const FModelAnimationDefinition& Definition = S.Document.Get();

		if ( S.BuiltModelPath != Definition.ModelPath || S.BuiltPreviewModelPath != Definition.PreviewModelPath )
		{
			// モデル（またはプレビュー用モデル）が変わった: 作り直す。読めなかったときも、同じパスで何度も試さない
			S.BuiltModelPath = Definition.ModelPath;
			S.BuiltPreviewModelPath = Definition.PreviewModelPath;
			S.PreviewModel = Definition.PreviewModelPath.IsEmpty() ? TSharedPtr<CModel>() : Assets->LoadModel( Definition.PreviewModelPath );
			const bool bHasAnything = !Definition.ModelPath.IsEmpty() || !Definition.Clips.IsEmpty() || S.PreviewModel.IsValid();
			S.Asset = bHasAnything ? Assets->CreateModelAnimations( Definition, "editor" ) : TSharedPtr<CModelAnimationAsset>();
			if ( S.Asset.IsValid() )
			{
				const TSharedPtr<CModel>& BoundModel = S.Asset->GetModel();
				if ( ( !BoundModel.IsValid() || BoundModel->IsAnimationOnly() ) && S.PreviewModel.IsValid() )
				{
					S.Animator = S.Asset->CreateAnimatorFor( S.PreviewModel );
				}
				else
				{
					S.Animator = S.Asset->CreateAnimator();
				}
			}
			else
			{
				S.Animator = FModelAnimator();
			}
			S.AssetRevision = S.Document.GetRevision();
			S.PlayingClip = INDEX_NONE;
			ComputeBounds( S );
			S.bFitRequested = true;
		}
		else if ( S.AssetRevision != S.Document.GetRevision() )
		{
			// 編集された: クリップと見た目の補正だけ入れ直す（モデルのインスタンスはそのまま）
			S.AssetRevision = S.Document.GetRevision();
			const bool bHasAnything = !Definition.ModelPath.IsEmpty() || !Definition.Clips.IsEmpty() || S.PreviewModel.IsValid();
			S.Asset = bHasAnything ? Assets->CreateModelAnimations( Definition, "editor" ) : TSharedPtr<CModelAnimationAsset>();
			if ( S.Asset.IsValid() )
			{
				const TSharedPtr<CModel>& BoundModel = S.Asset->GetModel();
				const TSharedPtr<CModel> Fallback = ( !BoundModel.IsValid() || BoundModel->IsAnimationOnly() ) ? S.PreviewModel : TSharedPtr<CModel>();
				if ( !S.Animator.Instance.IsValid() && Fallback.IsValid() ) S.Animator = S.Asset->CreateAnimatorFor( Fallback );
				else S.Asset->ApplyTo( S.Animator, Fallback );
			}
			if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) ) S.SelectedClip = Definition.Clips.IsEmpty() ? INDEX_NONE : Definition.Clips.Num() - 1;
			const FModelClipDef* Clip = SelectedClip( S );
			if ( Clip == nullptr || S.SelectedEvent >= Clip->Events.Num() ) S.SelectedEvent = INDEX_NONE;
			if ( !Definition.Clips.IsValidIndex( S.BlendFrom ) ) S.BlendFrom = INDEX_NONE;
			if ( !Definition.Clips.IsValidIndex( S.BlendTo ) ) S.BlendTo = INDEX_NONE;
			PlaySelected( S, true );
		}
		if ( S.PlayingClip != S.SelectedClip ) PlaySelected( S, false );
	}

	//=========================================================================
	// 開く・保存
	//=========================================================================
	void ResetSelection( FState& S )
	{
		S.SelectedClip = S.Document.Get().Clips.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedEvent = INDEX_NONE;
		S.BlendFrom = S.Document.Get().Clips.Num() > 0 ? 0 : INDEX_NONE;
		S.BlendTo = S.Document.Get().Clips.Num() > 1 ? 1 : INDEX_NONE;
		S.BuiltModelPath = "?";          // 必ず作り直す
		S.BuiltPreviewModelPath = "?";
		S.bFitRequested = true;
	}

	bool SaveCurrent( FState& S, const FString& AssetPath )
	{
		CAssetManager* Assets = GetAssets();
		if ( Assets == nullptr || AssetPath.IsEmpty() ) return false;

		FString Path = NormalizePath( AssetPath );
		if ( !Path.EndsWith( DefinitionExtension, ESearchCase::IgnoreCase ) ) Path += DefinitionExtension;
		if ( !Assets->SaveModelAnimationDefinition( Path, S.Document.Get() ) )
		{
			SetStatus( S, "Failed to save '" + Path + "' (see the log).", true );
			return false;
		}
		S.FilePath = Path;
		S.Document.MarkSaved();

		if ( S.bExportMV1OnSave )
		{
			const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( S.Document.Get() );
			if ( !Batch.Items.IsEmpty() )
			{
				SetStatus( S, FString::Printf( "Saved %s (.mv1 output: %s)", *Path, *Batch.GetSummary() ), Batch.NumFailed > 0 );
				RescanFiles( S, true );
				return true;
			}
		}
		SetStatus( S, "Saved " + Path );
		RescanFiles( S, true );
		return true;
	}

	void RequestAction( FState& S, FState::EPending Action, const FString& Path )
	{
		if ( S.Document.IsDirty() )
		{
			S.Pending = Action;
			S.PendingPath = Path;
			S.bRequestOpenUnsavedPopup = true;
			return;
		}
		if ( Action == FState::EPending::Open ) NDebugUI::OpenModelAnimationFile( S, Path );
		else if ( Action == FState::EPending::New ) NDebugUI::NewModelAnimationFromModel( S, Path );
		else if ( Action == FState::EPending::NewMotionPack ) NDebugUI::NewMotionPack( S, Path );
	}

	//=========================================================================
	// 3D プレビュー
	//=========================================================================
	void RenderPreview( FState& S, int32 Width, int32 Height )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr || Width < 16 || Height < 16 ) return;

		EnsureScreen( S, Width, Height );
		if ( S.PreviewScreen < 0 ) return;

		const float32 NearClip = GetCameraNear();
		const float32 FarClip = GetCameraFar();
		int R = 0, G = 0, B = 0;
		GetBackgroundColor( &R, &G, &B );

		SetDrawScreen( S.PreviewScreen );
		SetBackgroundColor( static_cast<int>( S.Background[0] * 255.0f ), static_cast<int>( S.Background[1] * 255.0f ), static_cast<int>( S.Background[2] * 255.0f ) );
		ClearDrawScreen();
		SetBackgroundColor( R, G, B );
		SetUseZBuffer3D( TRUE );
		SetWriteZBuffer3D( TRUE );

		const float32 CosPitch = NMath::Cos( S.Pitch );
		const VECTOR Target = VGet( S.Target.x, S.Target.y, S.Target.z );
		const VECTOR Eye = VGet( S.Target.x + S.Distance * CosPitch * NMath::Sin( S.Yaw ), S.Target.y + S.Distance * NMath::Sin( S.Pitch ), S.Target.z - S.Distance * CosPitch * NMath::Cos( S.Yaw ) );
		SetCameraNearFar( NMath::Max( S.Distance * 0.01f, 0.001f ), S.Distance * 200.0f );
		SetupCamera_Perspective( 0.8f );
		SetCameraPositionAndTargetAndUpVec( Eye, Target, VGet( 0.0f, 1.0f, 0.0f ) );

		// 床の格子と軸（モデルの大きさに合わせる）
		const float32 Cell = NMath::Max( NMath::Pow( 10.0f, NMath::Floor( NMath::Log10( NMath::Max( S.Distance * 0.25f, 0.001f ) ) ) ), 0.001f );
		if ( S.bShowFloor )
		{
			const int N = 10;
			for ( int i = -N; i <= N; ++i )
			{
				const unsigned int Color = i == 0 ? GetColor( 110, 110, 120 ) : GetColor( 60, 62, 70 );
				DrawLine3D( VGet( i * Cell, 0.0f, -N * Cell ), VGet( i * Cell, 0.0f, N * Cell ), Color );
				DrawLine3D( VGet( -N * Cell, 0.0f, i * Cell ), VGet( N * Cell, 0.0f, i * Cell ), Color );
			}
		}
		if ( S.bShowAxes )
		{
			DrawLine3D( VGet( 0.0f, 0.0f, 0.0f ), VGet( 3.0f * Cell, 0.0f, 0.0f ), GetColor( 255, 70, 70 ) );
			DrawLine3D( VGet( 0.0f, 0.0f, 0.0f ), VGet( 0.0f, 3.0f * Cell, 0.0f ), GetColor( 70, 255, 70 ) );
			DrawLine3D( VGet( 0.0f, 0.0f, 0.0f ), VGet( 0.0f, 0.0f, 3.0f * Cell ), GetColor( 80, 120, 255 ) );
		}

		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		S.Animator.Record( Draw, FMatrix4x4::Identity );
		Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );

		// 3D 描画の Z バッファは、既定（使わない）に戻す
		SetUseZBuffer3D( FALSE );
		SetWriteZBuffer3D( FALSE );
		SetDrawScreen( DX_SCREEN_BACK );
		SetCameraNearFar( NearClip, FarClip );
	}

	void DrawViewport( FState& S )
	{
		ImGui::Checkbox( "Floor", &S.bShowFloor );
		ImGui::SameLine();
		ImGui::Checkbox( "Axes", &S.bShowAxes );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Fit" ) ) S.bFitRequested = true;
		ImGui::SameLine();
		ImGui::ColorEdit3( "##Background", S.Background, ImGuiColorEditFlags_NoInputs );
		ImGui::SameLine();
		ImGui::TextDisabled( "drag: orbit   middle/right drag: move   wheel: zoom" );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImVec2 Size = ImGui::GetContentRegionAvail();
		if ( Size.x < 64.0f || Size.y < 64.0f ) return;

		EnsureScreen( S, static_cast<int32>( Size.x ), static_cast<int32>( Size.y ) );
		if ( S.bFitRequested ) FitCamera( S );
		RenderPreview( S, static_cast<int32>( Size.x ), static_cast<int32>( Size.y ) );

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max( Origin.x + Size.x, Origin.y + Size.y );
		if ( S.PreviewScreen >= 0 ) DrawList->AddImage( NDebugUI::GetImGuiTexture( S.PreviewScreen ), Origin, ImVec2( Origin.x + static_cast<float>( S.PreviewWidth ), Origin.y + static_cast<float>( S.PreviewHeight ) ) );

		ImGui::InvisibleButton( "##View", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
		ImGuiIO& IO = ImGui::GetIO();
		if ( ImGui::IsItemActive() )
		{
			if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			{
				S.Yaw -= IO.MouseDelta.x * 0.01f;
				S.Pitch = NMath::Clamp( S.Pitch + IO.MouseDelta.y * 0.01f, -1.5f, 1.5f );
			}
			else
			{
				// 画面に平行に動かす
				const float32 Scale = S.Distance * 0.0016f;
				const FVector3 Right( NMath::Cos( S.Yaw ), 0.0f, NMath::Sin( S.Yaw ) );
				const FVector3 Up( 0.0f, 1.0f, 0.0f );
				S.Target = S.Target - Right * ( IO.MouseDelta.x * Scale ) + Up * ( IO.MouseDelta.y * Scale );
			}
		}
		if ( ImGui::IsItemHovered() && IO.MouseWheel != 0.0f ) S.Distance = NMath::Clamp( S.Distance * ( IO.MouseWheel > 0.0f ? 0.88f : 1.0f / 0.88f ), 0.01f, 100000.0f );

		// 再生中に起きた出来事を、画面の隅に出す
		float Y = Origin.y + 6.0f;
		for ( const TPair<FString, float32>& Flash : S.EventFlashes )
		{
			DrawList->AddText( ImVec2( Origin.x + 8.0f, Y ), IM_COL32( 255, 220, 80, static_cast<int>( 255.0f * NMath::Min( Flash.Value * 2.0f, 1.0f ) ) ), UI_TEXT( "event: " + Flash.Key ) );
			Y += 14.0f;
		}
		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip != nullptr )
		{
			char Info[160];
			NCString::Format( Info, sizeof( Info ), "%s   %.0f%%   %s", *Clip->Name.ToUtf8(), S.Animator.GetNormalizedTime() * 100.0f, S.Animator.IsFinished() ? "finished" : ( S.bPlaying ? "playing" : "paused" ) );
			DrawList->AddText( ImVec2( Origin.x + 8.0f, Max.y - 18.0f ), IM_COL32( 230, 230, 230, 255 ), Info );
		}
		else if ( !S.Animator.Instance.IsValid() )
		{
			const bool bIsMotionPack = !S.Document.Get().Clips.IsEmpty() || ( S.Asset.IsValid() && S.Asset->GetModel().IsValid() && S.Asset->GetModel()->IsAnimationOnly() );
			const char* Hint = bIsMotionPack ? "Motion-only (no mesh). Pick a 'Preview model' in the Model tab to see 3D playback."
											 : "No model. Use File > New from model, File > New motion pack, or File > Open.";
			DrawList->AddText( ImVec2( Origin.x + 12.0f, Origin.y + 40.0f ), IM_COL32( 230, 230, 230, 255 ), Hint );
		}
	}

	//=========================================================================
	// タイムライン
	//=========================================================================
	void DrawTimeline( FState& S )
	{
		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip == nullptr )
		{
			ImGui::TextDisabled( "Select a clip to see its timeline." );
			return;
		}
		const int32 ClipIndex = S.SelectedClip;
		const float32 Length = S.Animator.GetClipLength( Clip->Name );

		// 再生の操作
		if ( ImGui::Button( S.bPlaying ? "Pause" : "Play" ) )
		{
			S.bPlaying = !S.bPlaying;
			if ( S.bPlaying ) S.Animator.Resume();
			else S.Animator.Pause();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Restart" ) )
		{
			PlaySelected( S, false );
			S.bPlaying = true;
			S.Animator.Resume();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 100.0f );
		ImGui::SliderFloat( "##Speed", &S.PreviewSpeed, 0.1f, 3.0f, "x%.2f" );
		ImGui::SameLine();
		ImGui::TextDisabled( "length %s", UI_TEXT( NDebugUI::FormatSeconds( Length ) ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Add event at playhead" ) ) S.SelectedEvent = NModelAnimationEdit::AddEvent( S.Document, ClipIndex, S.Animator.GetNormalizedTime(), S.NewEventName );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 120.0f );
		NDebugUI::InputString( "##NewEvent", S.NewEventName );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float Width = ImGui::GetContentRegionAvail().x;
		const float RulerHeight = 16.0f, FlagArea = 44.0f, TrackHeight = 26.0f;
		const float TotalHeight = RulerHeight + FlagArea + TrackHeight;
		const float TrackLeft = Origin.x + 8.0f;
		const float TrackWidth = Width - 16.0f;
		ImGuiIO& IO = ImGui::GetIO();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		ImGui::InvisibleButton( "##TimelineArea", ImVec2( Width, TotalHeight ) );
		const bool bScrubbing = ImGui::IsItemActive();
		DrawList->AddRectFilled( Origin, ImVec2( Origin.x + Width, Origin.y + TotalHeight ), IM_COL32( 30, 30, 34, 255 ) );

		// 目盛り（秒）
		if ( Length > 0.0f )
		{
			const float32 Step = Length > 8.0f ? 1.0f : ( Length > 2.0f ? 0.5f : ( Length > 0.5f ? 0.1f : 0.05f ) );
			for ( float32 T = 0.0f; T <= Length + 1.0e-4f; T += Step )
			{
				const float X = TrackLeft + T / Length * TrackWidth;
				DrawList->AddLine( ImVec2( X, Origin.y ), ImVec2( X, Origin.y + RulerHeight ), IM_COL32( 160, 160, 170, 255 ) );
				char Label[16];
				NCString::Format( Label, sizeof( Label ), "%.2f", T );
				DrawList->AddText( ImVec2( X + 2.0f, Origin.y ), IM_COL32( 160, 160, 170, 255 ), Label );
			}
		}
		const float TrackTop = Origin.y + RulerHeight + FlagArea;
		DrawList->AddRectFilled( ImVec2( TrackLeft, TrackTop ), ImVec2( TrackLeft + TrackWidth, TrackTop + TrackHeight ), IM_COL32( 52, 60, 78, 255 ) );

		// 出来事の旗（ドラッグで動かす。右クリックで削除）
		for ( int32 i = 0; i < Clip->Events.Num(); ++i )
		{
			const FModelEventDef& Event = Clip->Events[i];
			const float X = TrackLeft + Event.Time * TrackWidth;
			const float Y = Origin.y + RulerHeight + 2.0f + static_cast<float>( i % 3 ) * 14.0f;
			const bool bSelected = S.SelectedEvent == i;
			bool bFlash = false;
			for ( const TPair<FString, float32>& Flash : S.EventFlashes )
			{
				if ( Flash.Key == Event.Name ) bFlash = true;
			}
			const ImVec2 Flag0( X, Y ), Flag1( X + 10.0f + static_cast<float>( Event.Name.Len() ) * 6.5f, Y + 13.0f );
			DrawList->AddLine( ImVec2( X, Y ), ImVec2( X, TrackTop + TrackHeight ), bSelected || bFlash ? IM_COL32( 255, 220, 80, 255 ) : IM_COL32( 255, 150, 60, 200 ) );
			DrawList->AddRectFilled( Flag0, Flag1, bSelected || bFlash ? IM_COL32( 255, 210, 70, 255 ) : IM_COL32( 230, 120, 40, 255 ) );
			DrawList->AddText( ImVec2( X + 4.0f, Y ), IM_COL32( 20, 20, 20, 255 ), UI_TEXT( Event.Name ) );

			ImGui::SetCursorScreenPos( Flag0 );
			ImGui::PushID( 3000 + i );
			ImGui::InvisibleButton( "##Flag", ImVec2( Flag1.x - Flag0.x, Flag1.y - Flag0.y ) );
			if ( ImGui::IsItemClicked() ) S.SelectedEvent = i;
			if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 1.0f ) )
			{
				const float32 NewTime = NMath::Clamp( ( IO.MousePos.x - TrackLeft ) / TrackWidth, 0.0f, 1.0f );
				const FString Key = FString::Printf( "evdrag%d", i );
				const int32 NewIndex = NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, NewTime, Event.Name, *Key );
				if ( S.SelectedEvent == i ) S.SelectedEvent = NewIndex;
			}
			if ( ImGui::BeginPopupContextItem( "##FlagMenu" ) )
			{
				if ( ImGui::MenuItem( "Delete event" ) )
				{
					NModelAnimationEdit::RemoveEvent( S.Document, ClipIndex, i );
					S.SelectedEvent = INDEX_NONE;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// 再生位置
		const float32 Normalized = S.Animator.GetNormalizedTime();
		const float PlayheadX = TrackLeft + Normalized * TrackWidth;
		DrawList->AddLine( ImVec2( PlayheadX, Origin.y ), ImVec2( PlayheadX, Origin.y + TotalHeight ), IM_COL32( 255, 80, 80, 255 ), 2.0f );
		char TimeLabel[48];
		NCString::Format( TimeLabel, sizeof( TimeLabel ), "%.3f s (%.0f%%)", Normalized * Length, Normalized * 100.0f );
		DrawList->AddText( ImVec2( NMath::Min( PlayheadX + 4.0f, Origin.x + Width - 110.0f ), TrackTop + 6.0f ), IM_COL32( 255, 200, 200, 255 ), TimeLabel );

		// 背景のドラッグで、再生位置を動かす（動かすと一時停止）
		if ( bScrubbing && ImGui::IsMouseDown( ImGuiMouseButton_Left ) && Length > 0.0f )
		{
			bool bOnFlag = false;
			for ( int32 i = 0; i < Clip->Events.Num(); ++i ) bOnFlag = bOnFlag || ImGui::IsPopupOpen( "##FlagMenu" );
			if ( !bOnFlag )
			{
				S.bPlaying = false;
				S.Animator.Pause();
				S.Animator.Seek( NMath::Clamp( ( IO.MousePos.x - TrackLeft ) / TrackWidth, 0.0f, 1.0f ) );
			}
		}
		ImGui::SetCursorScreenPos( ImVec2( Origin.x, Origin.y + TotalHeight + 4.0f ) );

		// 出来事の一覧
		int32 RemoveEvent = INDEX_NONE;
		for ( int32 i = 0; i < Clip->Events.Num(); ++i )
		{
			ImGui::PushID( 4000 + i );
			const FModelEventDef& Event = Clip->Events[i];
			if ( ImGui::Selectable( "##Event", S.SelectedEvent == i, 0, ImVec2( 12.0f, 0.0f ) ) ) S.SelectedEvent = i;
			ImGui::SameLine();
			float32 Time = Event.Time;
			ImGui::SetNextItemWidth( 90.0f );
			if ( ImGui::DragFloat( "##Time", &Time, 0.002f, 0.0f, 1.0f, "%.3f" ) )
			{
				const FString Key = FString::Printf( "evtime%d", i );
				const int32 NewIndex = NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, Time, Event.Name, *Key );
				if ( S.SelectedEvent == i ) S.SelectedEvent = NewIndex;
			}
			ImGui::SameLine();
			ImGui::TextDisabled( "%6.3f s", Event.Time * Length );
			ImGui::SameLine();
			if ( S.EventNameBufferIndex != i || !ImGui::IsAnyItemActive() ) S.EventNameBuffer = Event.Name;
			FString Buffer = S.EventNameBufferIndex == i ? S.EventNameBuffer : Event.Name;
			ImGui::SetNextItemWidth( 150.0f );
			if ( NDebugUI::InputString( "##Name", Buffer ) )
			{
				S.EventNameBuffer = Buffer;
				S.EventNameBufferIndex = i;
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
			{
				NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, Event.Time, S.EventNameBuffer );
				S.EventNameBufferIndex = INDEX_NONE;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) ) RemoveEvent = i;
			ImGui::PopID();
		}
		if ( RemoveEvent != INDEX_NONE )
		{
			NModelAnimationEdit::RemoveEvent( S.Document, ClipIndex, RemoveEvent );
			S.SelectedEvent = INDEX_NONE;
		}
		if ( Clip->Events.IsEmpty() ) ImGui::TextDisabled( "No events. 'Add event at playhead' puts one where the playhead is (0..1 of the clip)." );
	}

	//=========================================================================
	// 左: モデル・クリップ
	//=========================================================================
	void DrawModelTab( FState& S )
	{
		CAssetManager* Assets = GetAssets();
		const FModelAnimationDefinition& Definition = S.Document.Get();
		ImGui::TextUnformatted( "Model" );
		if ( !Definition.ModelPath.IsEmpty() )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Clear (motion pack)" ) ) NModelAnimationEdit::SetModelPath( S.Document, FString() );
		}
		FString Selected = Definition.ModelPath;
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( NDebugUI::FileCombo( "##Model", Selected, S.ModelFiles, "(none - motion pack)" ) ) NModelAnimationEdit::SetModelPath( S.Document, Selected );

		bool bNeedPreviewModel = Definition.ModelPath.IsEmpty();
		if ( S.Asset.IsValid() && S.Asset->GetModel().IsValid() )
		{
			const TSharedPtr<CModel>& Model = S.Asset->GetModel();
			ImGui::Text( "%s, ~%s in memory", UI_TEXT( Model->Describe() ), UI_TEXT( NDebugUI::FormatBytes( Model->GetMemoryBytes() ) ) );
			if ( Model->IsAnimationOnly() )
			{
				bNeedPreviewModel = true;
				ImGui::TextColored( ImVec4( 0.55f, 0.85f, 1.0f, 1.0f ), "Animation-only file (0 meshes). Choose a Preview model below to see 3D playback." );
			}
		}
		else if ( !Definition.ModelPath.IsEmpty() )
		{
			NDebugUI::PushWarningColor();
			ImGui::TextWrapped( "The model could not be loaded (see the Output Log)." );
			NDebugUI::PopWarningColor();
		}
		else
		{
			ImGui::TextDisabled( "Motion pack (no bound model). Can be applied to any character with CreateAnimatorFor." );
		}

		if ( bNeedPreviewModel || !Definition.PreviewModelPath.IsEmpty() )
		{
			ImGui::Spacing();
			ImGui::TextUnformatted( "Preview model (editor only)" );
			if ( !Definition.PreviewModelPath.IsEmpty() )
			{
				ImGui::SameLine();
				if ( ImGui::SmallButton( "Clear##Preview" ) ) NModelAnimationEdit::SetPreviewModelPath( S.Document, FString() );
			}
			FString PreviewSelected = Definition.PreviewModelPath;
			ImGui::SetNextItemWidth( -FLT_MIN );
			if ( NDebugUI::FileCombo( "##PreviewModel", PreviewSelected, S.ModelFiles, "(choose a mesh model for 3D preview)" ) )
			{
				NModelAnimationEdit::SetPreviewModelPath( S.Document, PreviewSelected );
			}
			if ( S.PreviewModel.IsValid() && S.PreviewModel->IsAnimationOnly() )
			{
				NDebugUI::PushWarningColor();
				ImGui::TextWrapped( "The selected preview model also has 0 meshes. Pick a model with a mesh." );
				NDebugUI::PopWarningColor();
			}
		}

		float32 UnitsPerSecond = Definition.UnitsPerSecond;
		if ( ImGui::DragFloat( "Time units / s", &UnitsPerSecond, 0.1f, 0.001f, 1000.0f, "%.2f" ) ) NModelAnimationEdit::SetUnitsPerSecond( S.Document, UnitsPerSecond, "ups" );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The model's animation time is in frames. 30 per second is usual for FBX / MV1 / X." );

		ImGui::SeparatorText( "Placement (applied when drawn)" );
		FVector3 Offset = Definition.Offset, Rotation = Definition.RotationDegrees, Scale = Definition.Scale;
		bool bChanged = false;
		bChanged |= ImGui::DragFloat3( "Offset", &Offset.x, 0.01f );
		bChanged |= ImGui::DragFloat3( "Rotation", &Rotation.x, 0.5f, -360.0f, 360.0f, "%.1f deg" );
		bChanged |= ImGui::DragFloat3( "Scale", &Scale.x, 0.005f );
		if ( bChanged ) NModelAnimationEdit::SetPlacement( S.Document, Offset, Rotation, Scale, "placement" );

		ImGui::SeparatorText( ".mv1 output" );
		ImGui::Checkbox( "Also output .mv1 when saving", &S.bExportMV1OnSave );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "When saving this .modelanim.json, any .fbx / .obj / .x / .pmx / .vmd used by it is also saved as .mv1 next to the source file." );
		if ( Assets != nullptr )
		{
			if ( ImGui::Button( "Export .mv1 now" ) )
			{
				const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, false );
				SetStatus( S, ".mv1 export: " + Batch.GetSummary(), Batch.NumFailed > 0 );
				RescanFiles( S, true );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Force rebuild .mv1" ) )
			{
				const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, true );
				SetStatus( S, ".mv1 rebuild: " + Batch.GetSummary(), Batch.NumFailed > 0 );
				RescanFiles( S, true );
			}
		}

		ImGui::SeparatorText( "Check" );
		const TArray<FString> Problems = NModelAnimationEdit::Validate( Definition );
		if ( Problems.IsEmpty() )
		{
			ImGui::TextColored( ImVec4( 0.5f, 1.0f, 0.5f, 1.0f ), "No problems found." );
		}
		else
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.35f, 1.0f ), "%d problem(s):", Problems.Num() );
			for ( const FString& Problem : Problems ) ImGui::TextWrapped( "- %s", UI_TEXT( Problem ) );
		}
		// 定義が指すクリップが、モデルに実在するか・骨の名前が合うか
		if ( S.Asset.IsValid() )
		{
			const TSharedPtr<CModel> TargetMesh = GetEffectiveMeshModel( S );
			for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
			{
				const FModelClipDef& Clip = Definition.Clips[i];
				if ( !S.Animator.HasClip( Clip.Name ) || S.Animator.GetClipLength( Clip.Name ) <= 0.0f )
				{
					NDebugUI::PushWarningColor();
					ImGui::TextWrapped( "- Clip '%s': the animation was not found in its file.", UI_TEXT( Clip.Name ) );
					NDebugUI::PopWarningColor();
				}
				const TSharedPtr<CModel> SourceModel = S.Asset->GetClipSource( i );
				if ( TargetMesh.IsValid() && SourceModel.IsValid() && SourceModel != TargetMesh )
				{
					const FBoneMatch Match = TargetMesh->CompareBones( *SourceModel );
					if ( Match.IsPoor() )
					{
						NDebugUI::PushWarningColor();
						ImGui::TextWrapped( "- Clip '%s': only %d / %d bone(s) match '%s'.", UI_TEXT( Clip.Name ), Match.NumMatched, Match.NumAnimBones, UI_TEXT( TargetMesh->GetPath() ) );
						NDebugUI::PopWarningColor();
					}
				}
			}
		}
	}

	void DrawClipsTab( FState& S )
	{
		CAssetManager* Assets = GetAssets();
		const FModelAnimationDefinition& Definition = S.Document.Get();
		bool bOpenAddPopup = false;
		if ( ImGui::Button( "Add clip..." ) ) bOpenAddPopup = true;
		ImGui::SameLine();
		ImGui::BeginDisabled( !Definition.Clips.IsValidIndex( S.SelectedClip ) );
		if ( ImGui::Button( "Delete" ) )
		{
			NModelAnimationEdit::RemoveClip( S.Document, S.SelectedClip );
			S.SelectedClip = NMath::Min( S.SelectedClip, S.Document.Get().Clips.Num() - 1 );
			S.SelectedEvent = INDEX_NONE;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Up" ) && S.SelectedClip > 0 )
		{
			NModelAnimationEdit::MoveClip( S.Document, S.SelectedClip, S.SelectedClip - 1 );
			--S.SelectedClip;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Down" ) && S.SelectedClip + 1 < Definition.Clips.Num() )
		{
			NModelAnimationEdit::MoveClip( S.Document, S.SelectedClip, S.SelectedClip + 1 );
			++S.SelectedClip;
		}
		ImGui::EndDisabled();

		if ( bOpenAddPopup )
		{
			S.AddSourceSelection.Empty();
			ImGui::OpenPopup( "Add clip" );
		}
		ImGui::SetNextWindowSize( ImVec2( 500.0f, 380.0f ), ImGuiCond_Appearing );
		if ( ImGui::BeginPopupModal( "Add clip", nullptr ) )
		{
			ImGui::TextUnformatted( "Add an animation from:" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			NDebugUI::FileCombo( "##Source", S.AddSourceSelection, S.ModelFiles, Definition.ModelPath.IsEmpty() ? "(choose an animation / model file)" : "(the model itself)" );
			if ( !Definition.ModelPath.IsEmpty() && ImGui::SmallButton( "the model itself" ) ) S.AddSourceSelection.Empty();

			const FString EffectiveSourcePath = S.AddSourceSelection.IsEmpty() ? Definition.ModelPath : S.AddSourceSelection;
			TSharedPtr<CModel> Source;
			if ( Assets != nullptr && !EffectiveSourcePath.IsEmpty() ) Source = Assets->LoadModel( EffectiveSourcePath );
			if ( Source.IsValid() )
			{
				ImGui::TextDisabled( "%s   (%d bone(s))", UI_TEXT( Source->Describe() ), Source->GetBoneNames().Num() );
				const TSharedPtr<CModel> TargetMesh = GetEffectiveMeshModel( S );
				if ( TargetMesh.IsValid() && Source != TargetMesh )
				{
					const FBoneMatch Match = TargetMesh->CompareBones( *Source );
					if ( Match.IsPoor() )
					{
						NDebugUI::PushWarningColor();
						ImGui::TextWrapped( "Bone match with '%s': %d / %d (%.0f%%) - most bones do not match.", UI_TEXT( TargetMesh->GetPath() ), Match.NumMatched, Match.NumAnimBones, Match.GetRatio() * 100.0f );
						NDebugUI::PopWarningColor();
					}
					else
					{
						ImGui::TextColored( ImVec4( 0.55f, 0.95f, 0.65f, 1.0f ), "Bone match with '%s': %d / %d (%.0f%%)", UI_TEXT( TargetMesh->GetPath() ), Match.NumMatched, Match.NumAnimBones, Match.GetRatio() * 100.0f );
					}
				}
				if ( Source->GetClips().Num() > 1 )
				{
					char BatchLabel[96];
					NCString::Format( BatchLabel, sizeof( BatchLabel ), "Add all animations (%d)", Source->GetClips().Num() );
					if ( ImGui::Button( BatchLabel ) )
					{
						TArray<FModelClipDef> Batch;
						for ( const FModelClipInfo& Info : Source->GetClips() )
						{
							FModelClipDef Clip;
							Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
							Clip.SourcePath = S.AddSourceSelection;
							Clip.AnimIndex = Info.AnimIndex;
							Clip.AnimName = Info.Name;
							Batch.Add( Clip );
						}
						S.SelectedClip = NModelAnimationEdit::AddClips( S.Document, Batch );
						S.SelectedEvent = INDEX_NONE;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::BeginChild( "##Animations", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders );
			if ( Source.IsValid() )
			{
				if ( Source->GetClips().IsEmpty() ) ImGui::TextDisabled( "This file has no animations." );
				for ( const FModelClipInfo& Info : Source->GetClips() )
				{
					char Label[200];
					NCString::Format( Label, sizeof( Label ), "%s   (#%d, %.0f time units)###anim%d", Info.Name.IsEmpty() ? "(unnamed)" : *Info.Name.ToUtf8(), Info.AnimIndex, Info.Length, Info.AnimIndex );
					if ( ImGui::Selectable( Label ) )
					{
						FModelClipDef Clip;
						Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
						Clip.SourcePath = S.AddSourceSelection;
						Clip.AnimIndex = Info.AnimIndex;
						Clip.AnimName = Info.Name;
						S.SelectedClip = NModelAnimationEdit::AddClip( S.Document, Clip );
						S.SelectedEvent = INDEX_NONE;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			else if ( EffectiveSourcePath.IsEmpty() )
			{
				ImGui::TextDisabled( "Choose a model or animation file above." );
			}
			else
			{
				ImGui::TextDisabled( "The file could not be loaded." );
			}
			ImGui::EndChild();
			if ( ImGui::Button( "Close" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		ImGui::BeginChild( "##ClipList", ImVec2( 0.0f, 120.0f ), ImGuiChildFlags_Borders );
		for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
		{
			const FModelClipDef& Clip = Definition.Clips[i];
			char Label[200];
			NCString::Format( Label, sizeof( Label ), "%s   (%.2f s, %s, %d event(s))###clip%d", *Clip.Name.ToUtf8(), S.Animator.GetClipLength( Clip.Name ), ToString( Clip.Loop ), Clip.Events.Num(), i );
			if ( ImGui::Selectable( Label, S.SelectedClip == i ) )
			{
				S.SelectedClip = i;
				S.SelectedEvent = INDEX_NONE;
			}
		}
		if ( Definition.Clips.IsEmpty() ) ImGui::TextDisabled( "No clips yet. Click 'Add clip...'." );
		ImGui::EndChild();

		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip == nullptr ) return;
		const int32 ClipIndex = S.SelectedClip;

		if ( !ImGui::IsAnyItemActive() ) S.NameBuffer = Clip->Name;
		NDebugUI::InputString( "Name", S.NameBuffer );
		if ( ImGui::IsItemDeactivatedAfterEdit() ) S.NameBuffer = NModelAnimationEdit::RenameClip( S.Document, ClipIndex, S.NameBuffer );

		FModelClipDef Edited = *Clip;
		bool bChanged = false;

		// アニメーションの入ったファイルと、その中のアニメーション
		FString Source = Edited.SourcePath;
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::TextUnformatted( "Animation file" );
		if ( NDebugUI::FileCombo( "##SourceFile", Source, S.ModelFiles, "(the model itself)" ) )
		{
			Edited.SourcePath = Source;
			bChanged = true;
		}
		TSharedPtr<CModel> SourceModel = S.Asset.IsValid() ? S.Asset->GetClipSource( ClipIndex ) : TSharedPtr<CModel>();
		if ( SourceModel.IsValid() )
		{
			if ( SourceModel->IsAnimationOnly() ) ImGui::TextDisabled( "Animation-only source (%d bone(s))", SourceModel->GetBoneNames().Num() );
			const char* Preview = Edited.AnimName.IsEmpty() ? "(by number)" : *Edited.AnimName.ToUtf8();
			if ( ImGui::BeginCombo( "Animation", Preview ) )
			{
				for ( const FModelClipInfo& Info : SourceModel->GetClips() )
				{
					char Label[160];
					NCString::Format( Label, sizeof( Label ), "%s  (#%d)###a%d", Info.Name.IsEmpty() ? "(unnamed)" : *Info.Name.ToUtf8(), Info.AnimIndex, Info.AnimIndex );
					if ( ImGui::Selectable( Label, Info.AnimIndex == Edited.AnimIndex ) )
					{
						Edited.AnimIndex = Info.AnimIndex;
						Edited.AnimName = Info.Name;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
		}
		int Loop = static_cast<int>( Edited.Loop );
		if ( ImGui::Combo( "Loop", &Loop, "Once\0Loop\0Ping-pong\0" ) )
		{
			Edited.Loop = static_cast<EAnimLoopMode>( Loop );
			bChanged = true;
		}
		bChanged |= ImGui::DragFloat( "Speed", &Edited.Speed, 0.01f, -4.0f, 4.0f, "x%.2f" );
		bChanged |= ImGui::DragFloat( "Fade in", &Edited.FadeTime, 0.01f, 0.0f, 5.0f, "%.2f s" );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The crossfade time when the game plays this clip without specifying one." );
		if ( bChanged ) NModelAnimationEdit::SetClipSettings( S.Document, ClipIndex, Edited, "clip" );

		// クロスフェードの確認
		ImGui::SeparatorText( "Crossfade test" );
		const auto ClipCombo = [&]( const char* Label, int32& Index )
		{
			const char* Preview = Definition.Clips.IsValidIndex( Index ) ? *Definition.Clips[Index].Name.ToUtf8() : "(none)";
			if ( ImGui::BeginCombo( Label, Preview ) )
			{
				for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
				{
					if ( ImGui::Selectable( *Definition.Clips[i].Name.ToUtf8(), Index == i ) ) Index = i;
				}
				ImGui::EndCombo();
			}
		};
		ClipCombo( "From", S.BlendFrom );
		ClipCombo( "To", S.BlendTo );
		ImGui::DragFloat( "Fade time", &S.BlendFade, 0.01f, 0.0f, 5.0f, "%.2f s" );
		const bool bCanBlend = Definition.Clips.IsValidIndex( S.BlendFrom ) && Definition.Clips.IsValidIndex( S.BlendTo );
		ImGui::BeginDisabled( !bCanBlend );
		if ( ImGui::Button( "Play From, then fade to To" ) )
		{
			FModelPlayParams First;
			First.FadeTime = 0.0f;
			First.bRestartIfPlaying = true;
			S.Animator.Play( Definition.Clips[S.BlendFrom].Name, First );
			S.Animator.Update( 0.0f );
			FModelPlayParams Second;
			Second.FadeTime = S.BlendFade;
			Second.bRestartIfPlaying = true;
			S.Animator.Play( Definition.Clips[S.BlendTo].Name, Second );
			S.bPlaying = true;
			S.Animator.Resume();
			S.PlayingClip = S.SelectedClip;   // 選択中のクリップに切り替わり直さない
		}
		ImGui::EndDisabled();
		for ( const FModelAnimator::FLayerInfo& Layer : S.Animator.GetLayerInfos() )
		{
			if ( Layer.Weight <= 0.0f ) continue;
			char Overlay[96];
			NCString::Format( Overlay, sizeof( Overlay ), "%s  %.0f%%", *Layer.ClipName.ToUtf8(), Layer.Weight * 100.0f );
			ImGui::ProgressBar( Layer.Weight, ImVec2( -FLT_MIN, 0.0f ), Overlay );
		}
	}

	//=========================================================================
	// メニュー
	//=========================================================================
	void DrawMenuBar( FState& S, bool* pOpen )
	{
		if ( !ImGui::BeginMenuBar() ) return;
		bool bOpenNewPopup = false, bOpenOpenPopup = false, bOpenSaveAsPopup = false;
		const FModelAnimationDefinition& Definition = S.Document.Get();
		const bool bCanSave = !Definition.ModelPath.IsEmpty() || !Definition.Clips.IsEmpty();

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "New from model..." ) ) bOpenNewPopup = true;
			if ( ImGui::MenuItem( "New motion pack (no model)" ) ) RequestAction( S, FState::EPending::NewMotionPack, FString() );
			if ( ImGui::MenuItem( "Open..." ) ) bOpenOpenPopup = true;
			ImGui::Separator();
			if ( ImGui::MenuItem( "Save", "Ctrl+S", false, bCanSave ) )
			{
				if ( S.FilePath.IsEmpty() ) bOpenSaveAsPopup = true;
				else SaveCurrent( S, S.FilePath );
			}
			if ( ImGui::MenuItem( "Save as...", nullptr, false, bCanSave ) ) bOpenSaveAsPopup = true;
			if ( ImGui::MenuItem( "Export used models to .mv1", nullptr, false, bCanSave ) )
			{
				if ( CAssetManager* Assets = GetAssets() )
				{
					const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, false );
					SetStatus( S, ".mv1 export: " + Batch.GetSummary(), Batch.NumFailed > 0 );
					RescanFiles( S, true );
				}
			}
			if ( ImGui::MenuItem( "Reload from disk", nullptr, false, !S.FilePath.IsEmpty() ) ) RequestAction( S, FState::EPending::Open, S.FilePath );
			ImGui::Separator();
			if ( ImGui::MenuItem( "Close window" ) && pOpen != nullptr ) *pOpen = false;
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, S.Document.CanUndo() ) ) S.Document.Undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, S.Document.CanRedo() ) ) S.Document.Redo();
			ImGui::EndMenu();
		}
		ImGui::TextDisabled( "  %s%s", S.FilePath.IsEmpty() ? "(unsaved)" : UI_TEXT( S.FilePath ), S.Document.IsDirty() ? " *" : "" );
		ImGui::EndMenuBar();

		if ( bOpenNewPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "New model animation set" );
		}
		if ( bOpenOpenPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "Open model animation set" );
		}
		if ( bOpenSaveAsPopup )
		{
			if ( S.SaveAsPath.IsEmpty() )
			{
				FString Base = Definition.ModelPath;
				if ( Base.IsEmpty() && !Definition.Clips.IsEmpty() ) Base = Definition.Clips[0].SourcePath;
				if ( Base.IsEmpty() ) Base = "Motions/MotionPack";
				const int32 Dot = Base.FindLast( "." );
				if ( Dot > 0 ) Base = Base.Left( Dot );
				S.SaveAsPath = Base + DefinitionExtension;
			}
			ImGui::OpenPopup( "Save model animation set as" );
		}
		if ( S.bRequestOpenUnsavedPopup )
		{
			ImGui::OpenPopup( "Unsaved changes" );
			S.bRequestOpenUnsavedPopup = false;
		}

		const ImVec2 PopupSize( 460.0f, 0.0f );
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "New model animation set", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose the model (or an animation-only file for a motion pack):" );
			NDebugUI::FileCombo( "##NewModel", S.NewModelSelection, S.ModelFiles, "(choose a model)" );
			ImGui::BeginDisabled( S.NewModelSelection.IsEmpty() );
			if ( ImGui::Button( "Create" ) )
			{
				RequestAction( S, FState::EPending::New, S.NewModelSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Open model animation set", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose a definition (*.modelanim.json under the asset folder):" );
			NDebugUI::FileCombo( "##OpenDefinition", S.OpenSelection, S.DefinitionFiles, "(choose a file)" );
			ImGui::BeginDisabled( S.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				RequestAction( S, FState::EPending::Open, S.OpenSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Save model animation set as", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Path under the asset folder (the .modelanim.json extension is added):" );
			NDebugUI::InputString( "##SaveAs", S.SaveAsPath );
			ImGui::Checkbox( "Also output referenced models as .mv1", &S.bExportMV1OnSave );
			if ( ImGui::Button( "Save" ) && !S.SaveAsPath.IsEmpty() )
			{
				if ( SaveCurrent( S, S.SaveAsPath ) ) ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopupModal( "Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "The model animation set has unsaved changes." );
			const auto Continue = [&]()
			{
				const FState::EPending Action = S.Pending;
				const FString Path = S.PendingPath;
				S.Pending = FState::EPending::None;
				if ( Action == FState::EPending::Open ) NDebugUI::OpenModelAnimationFile( S, Path );
				else if ( Action == FState::EPending::New ) NDebugUI::NewModelAnimationFromModel( S, Path );
				else if ( Action == FState::EPending::NewMotionPack ) NDebugUI::NewMotionPack( S, Path );
				else if ( Action == FState::EPending::Close && pOpen != nullptr ) *pOpen = false;
			};
			if ( ImGui::Button( "Save and continue" ) )
			{
				if ( !S.FilePath.IsEmpty() && SaveCurrent( S, S.FilePath ) )
				{
					Continue();
					ImGui::CloseCurrentPopup();
				}
				else if ( S.FilePath.IsEmpty() )
				{
					SetStatus( S, "Use File > Save as... first (this set has no file yet).", true );
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Discard changes" ) )
			{
				S.Document.MarkSaved();
				Continue();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				S.Pending = FState::EPending::None;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

//=============================================================================
// 公開
//=============================================================================
bool NDebugUI::OpenModelAnimationFile( FModelAnimationEditorState& State, const FString& AssetPath )
{
	CAssetManager* Assets = GetAssets();
	if ( Assets == nullptr ) return false;

	const TOptional<FString> Full = Assets->ResolvePath( AssetPath );
	if ( !Full.IsSet() )
	{
		SetStatus( State, "File not found: " + AssetPath, true );
		return false;
	}
	NJson::CJsonObject Json;
	FModelAnimationDefinition Definition;
	if ( !Json.LoadFromFile( Full.Get( FString() ) ) || !FModelAnimationDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		SetStatus( State, "Not a valid model animation definition: " + AssetPath, true );
		return false;
	}

	State.Document.Reset( Definition );
	State.FilePath = NormalizePath( AssetPath );
	ResetSelection( State );
	SetStatus( State, "Opened " + State.FilePath );
	return true;
}

bool NDebugUI::NewModelAnimationFromModel( FModelAnimationEditorState& State, const FString& ModelPath )
{
	const FString Normalized = NormalizePath( ModelPath );
	FModelAnimationDefinition Definition;
	CAssetManager* Assets = GetAssets();
	const TSharedPtr<CModel> Loaded = ( Assets != nullptr && !Normalized.IsEmpty() ) ? Assets->LoadModel( Normalized ) : TSharedPtr<CModel>();
	if ( Loaded.IsValid() && Loaded->IsAnimationOnly() )
	{
		// アニメーションだけのファイル（メッシュ 0）が選ばれたら、モーション専用パックにして全アニメーションを入れる
		for ( const FModelClipInfo& Info : Loaded->GetClips() )
		{
			FModelClipDef Clip;
			Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
			Clip.SourcePath = Normalized;
			Clip.AnimIndex = Info.AnimIndex;
			Clip.AnimName = Info.Name;
			Definition.Clips.Add( Clip );
		}
		State.Document.Reset( Definition );
		State.FilePath.Empty();
		State.SaveAsPath.Empty();
		ResetSelection( State );
		SetStatus( State, "New motion pack from animation-only file " + Normalized + ". Pick a Preview model in the Model tab to view in 3D." );
		return true;
	}

	Definition.ModelPath = Normalized;
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New animation set for " + Definition.ModelPath + ". Add clips from the model, then File > Save as." );
	return true;
}

bool NDebugUI::NewMotionPack( FModelAnimationEditorState& State, const FString& PreviewModelPath )
{
	FModelAnimationDefinition Definition;
	Definition.PreviewModelPath = NormalizePath( PreviewModelPath );
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New motion pack (no bound model). Add clips from animation files, and optionally pick a Preview model." );
	return true;
}

void NDebugUI::ReleaseModelAnimationEditor( FModelAnimationEditorState& State )
{
	if ( State.PreviewScreen >= 0 )
	{
		DeleteGraph( State.PreviewScreen );
		State.PreviewScreen = -1;
	}
	State.Animator = FModelAnimator();
	State.Asset.Reset();
	State.PreviewModel.Reset();
}

void NDebugUI::DrawModelAnimationEditor( FModelAnimationEditorState& S, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 16.0f, MainViewport->Pos.y + 38.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 1320.0f, MainViewport->Size.x - 32.0f ), NMath::Min( 780.0f, MainViewport->Size.y - 50.0f ) ), ImGuiCond_FirstUseEver );
	char Title[256];
	NCString::Format( Title, sizeof( Title ), "Model Animation Editor%s###ModelAnimationEditor", S.Document.IsDirty() ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen, ImGuiWindowFlags_MenuBar ) )
	{
		ImGui::End();
		return;
	}

	RescanFiles( S, false );
	SyncPreview( S );
	DrawMenuBar( S, pOpen );

	ImGuiIO& IO = ImGui::GetIO();
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput )
	{
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z ) ) S.Document.Undo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Y ) || ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z ) ) S.Document.Redo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_S ) && !S.FilePath.IsEmpty() ) SaveCurrent( S, S.FilePath );
	}

	// 再生（速さはプレビューのスライダー。出来事を集める）
	S.Animator.Speed = S.PreviewSpeed;
	if ( S.bPlaying && SelectedClip( S ) != nullptr )
	{
		S.Animator.Update( IO.DeltaTime );
		for ( const FString& Event : S.Animator.GetEvents() )
		{
			S.EventFlashes.Add( TPair<FString, float32> { Event, 0.5f } );
		}
	}
	for ( int32 i = S.EventFlashes.Num() - 1; i >= 0; --i )
	{
		S.EventFlashes[i].Value -= IO.DeltaTime;
		if ( S.EventFlashes[i].Value <= 0.0f ) S.EventFlashes.RemoveAt( i );
	}

	const float FooterHeight = ImGui::GetFrameHeightWithSpacing() + 2.0f;
	const float LeftWidth = 380.0f;

	ImGui::BeginChild( "##Left", ImVec2( LeftWidth, -FooterHeight ), ImGuiChildFlags_Borders );
	if ( ImGui::BeginTabBar( "##LeftTabs" ) )
	{
		const auto TabFlags = [&S]( int32 Index ) { return S.ForceLeftTab == Index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
		if ( ImGui::BeginTabItem( "Clips", nullptr, TabFlags( 0 ) ) ) { DrawClipsTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Model", nullptr, TabFlags( 1 ) ) ) { DrawModelTab( S ); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
		S.ForceLeftTab = INDEX_NONE;
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild( "##Right", ImVec2( 0.0f, -FooterHeight ) );
	ImGui::BeginChild( "##Viewport", ImVec2( 0.0f, -230.0f ), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	DrawViewport( S );
	ImGui::EndChild();
	ImGui::BeginChild( "##Timeline", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	DrawTimeline( S );
	ImGui::EndChild();
	ImGui::EndChild();

	if ( S.bStatusIsError ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
	else ImGui::TextDisabled( "%s", UI_TEXT( S.StatusMessage ) );

	if ( !ImGui::IsAnyItemActive() ) S.Document.EndEditSession();
	ImGui::End();
}
