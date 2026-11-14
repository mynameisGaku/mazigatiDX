#include "ModelAnimationEditorInternal.h"

namespace NModelAnimationEditorUI
{
	//=========================================================================
	// 定義 -> 資産 -> アニメーター
	//=========================================================================
	void ComputeBounds( FState& S )
	{
		S.bHaveBounds = false;
		if ( !S.Animator.Instance.IsValid() || !S.Animator.Instance->GetSource().IsValid() )
		{
			return;
		}
		// インスタンスのメッシュは、元のモデルと同じ
		FVector3 Min;
		FVector3 Max;
		if ( !S.Animator.Instance->GetSource()->GetBounds( Min, Max ) )
		{
			return;
		}
		S.BoundsMin = Min;
		S.BoundsMax = Max;
		S.bHaveBounds = true;
	}

	void EnsureScreen( FState& S, int32 Width, int32 Height )
	{
		if ( S.PreviewTarget.IsValid() && S.PreviewWidth == Width && S.PreviewHeight == Height )
		{
			return;
		}
		// 前の描画先を手放してから、新しい大きさで作る（同じ名前を使い回す）
		S.PreviewTarget.Reset();
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		if ( Assets == nullptr )
		{
			return;
		}
		FRenderTargetDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.bAlpha = false;
		Desc.bDepth = true;
		S.PreviewTarget = Assets->CreateRenderTarget( "Editor/ModelAnimation/Preview", Desc );
		S.PreviewWidth = Width;
		S.PreviewHeight = Height;
	}

	// 選んだクリップを頭から終わりまで少しずつ動かして、モデルの各フレーム（ボーン）がどこを通るかを調べる。
	// 画面は使わない（描画の実行でアニメーションの時間が入る。座標だけ読む）
	bool SampleMotionRange( FState& S, FVector3& OutMin, FVector3& OutMax )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr || !S.Animator.Instance.IsValid() || !S.PreviewTarget.IsValid() || SelectedClip( S ) == nullptr )
		{
			return false;
		}

		const float32 Saved = S.Animator.GetNormalizedTime();
		OutMin = FVector3( 1.0e30f, 1.0e30f, 1.0e30f );
		OutMax = FVector3( -1.0e30f, -1.0e30f, -1.0e30f );
		constexpr int32 NumSamples = 24;
		TArray<FVector3> Positions;
		for ( int32 i = 0; i <= NumSamples; ++i )
		{
			// 描画先へ描いて（描く実行で、アニメーションの時間が骨に入る）、骨の位置を読む
			S.Animator.Seek( static_cast<float32>( i ) / static_cast<float32>( NumSamples ) );
			FDrawBuffer Buffer;
			{
				FDrawContext Draw( Buffer );
				if ( Draw.PushTarget( S.PreviewTarget ) )
				{
					S.Animator.Record( Draw, FMatrix4x4::Identity );
					Draw.PopTarget();
				}
			}
			Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
			if ( !S.Animator.Instance->GetFramePositions( Positions ) )
			{
				continue;
			}
			for ( const FVector3& Position : Positions )
			{
				OutMin = FVector3( NMath::Min( OutMin.x, Position.x ), NMath::Min( OutMin.y, Position.y ), NMath::Min( OutMin.z, Position.z ) );
				OutMax = FVector3( NMath::Max( OutMax.x, Position.x ), NMath::Max( OutMax.y, Position.y ), NMath::Max( OutMax.z, Position.z ) );
			}
		}
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
		if ( !S.Animator.Play( Clip->Name, Params ) )
		{
			return;
		}
		if ( bKeepTime )
		{
			S.Animator.Seek( Normalized );
		}
		if ( S.bPlaying )
		{
			S.Animator.Resume();
		}
		else
		{
			S.Animator.Pause();
		}
	}

	void SyncPreview( FState& S )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr )
		{
			return;
		}
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
				if ( !S.Animator.Instance.IsValid() && Fallback.IsValid() )
				{
					S.Animator = S.Asset->CreateAnimatorFor( Fallback );
				}
				else
				{
					S.Asset->ApplyTo( S.Animator, Fallback );
				}
			}
			if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) )
			{
				S.SelectedClip = Definition.Clips.IsEmpty() ? INDEX_NONE : Definition.Clips.Num() - 1;
			}
			const FModelClipDef* Clip = SelectedClip( S );
			if ( Clip == nullptr || S.SelectedEvent >= Clip->Events.Num() )
			{
				S.SelectedEvent = INDEX_NONE;
			}
			if ( !Definition.Clips.IsValidIndex( S.BlendFrom ) )
			{
				S.BlendFrom = INDEX_NONE;
			}
			if ( !Definition.Clips.IsValidIndex( S.BlendTo ) )
			{
				S.BlendTo = INDEX_NONE;
			}
			PlaySelected( S, true );
		}
		if ( S.PlayingClip != S.SelectedClip )
		{
			PlaySelected( S, false );
		}
	}

	//=========================================================================
	// 3D プレビュー
	//=========================================================================
	void RenderPreview( FState& S, int32 Width, int32 Height )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr || Width < 16 || Height < 16 )
		{
			return;
		}
		EnsureScreen( S, Width, Height );
		if ( !S.PreviewTarget.IsValid() )
		{
			return;
		}

		// 注視点のまわりを回るカメラ
		const float32 CosPitch = NMath::Cos( S.Pitch );
		const FVector3 Eye( S.Target.x + S.Distance * CosPitch * NMath::Sin( S.Yaw ), S.Target.y + S.Distance * NMath::Sin( S.Pitch ), S.Target.z - S.Distance * CosPitch * NMath::Cos( S.Yaw ) );
		FCamera Camera = FCamera::MakePerspective( Eye, S.Target, 0.8f );
		Camera.Near = NMath::Max( S.Distance * 0.01f, 0.001f );
		Camera.Far = S.Distance * 200.0f;

		FDrawBuffer Buffer;
		{
			FDrawContext Draw( Buffer );
			if ( !Draw.PushTarget( S.PreviewTarget ) )
			{
				return;
			}
			const auto ToByte = []( float32 Value )
			{
				return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f );
			};
			Draw.Clear( FColor32( ToByte( S.Background[0] ), ToByte( S.Background[1] ), ToByte( S.Background[2] ), 255 ) );
			Draw.ClearDepth();
			FDepthState DepthOn;
			DepthOn.bTestDepth = true;
			DepthOn.bWriteDepth = true;
			Draw.Depth( DepthOn );
			const bool bCamera = Draw.PushCamera( Camera );

			// 床の格子と軸（モデルの大きさに合わせる）
			const float32 Cell = NMath::Max( NMath::Pow( 10.0f, NMath::Floor( NMath::Log10( NMath::Max( S.Distance * 0.25f, 0.001f ) ) ) ), 0.001f );
			if ( S.bShowFloor )
			{
				const int N = 10;
				for ( int i = -N; i <= N; ++i )
				{
					const FColor32 Color = i == 0 ? FColor32( 110, 110, 120 ) : FColor32( 60, 62, 70 );
					const float32 Offset = static_cast<float32>( i ) * Cell;
					const float32 Edge = static_cast<float32>( N ) * Cell;
					Draw.Line3D( FVector3( Offset, 0.0f, -Edge ), FVector3( Offset, 0.0f, Edge ), Color );
					Draw.Line3D( FVector3( -Edge, 0.0f, Offset ), FVector3( Edge, 0.0f, Offset ), Color );
				}
			}
			if ( S.bShowAxes )
			{
				Draw.Line3D( FVector3::Zero, FVector3( 3.0f * Cell, 0.0f, 0.0f ), FColor32( 255, 70, 70 ) );
				Draw.Line3D( FVector3::Zero, FVector3( 0.0f, 3.0f * Cell, 0.0f ), FColor32( 70, 255, 70 ) );
				Draw.Line3D( FVector3::Zero, FVector3( 0.0f, 0.0f, 3.0f * Cell ), FColor32( 80, 120, 255 ) );
			}

			S.Animator.Record( Draw, FMatrix4x4::Identity );
			if ( bCamera )
			{
				Draw.PopCamera();
			}
			Draw.PopTarget();
		}
		Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
	}

	void DrawViewport( FState& S )
	{
		ImGui::Checkbox( "Floor", &S.bShowFloor );
		ImGui::SameLine();
		ImGui::Checkbox( "Axes", &S.bShowAxes );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Fit" ) )
		{
			S.bFitRequested = true;
		}
		ImGui::SameLine();
		ImGui::ColorEdit3( "##Background", S.Background, ImGuiColorEditFlags_NoInputs );
		ImGui::SameLine();
		ImGui::TextDisabled( "drag: orbit   middle/right drag: move   wheel: zoom" );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImVec2 Size = ImGui::GetContentRegionAvail();
		if ( Size.x < 64.0f || Size.y < 64.0f )
		{
			return;
		}

		EnsureScreen( S, static_cast<int32>( Size.x ), static_cast<int32>( Size.y ) );
		if ( S.bFitRequested )
		{
			FitCamera( S );
		}
		RenderPreview( S, static_cast<int32>( Size.x ), static_cast<int32>( Size.y ) );

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max( Origin.x + Size.x, Origin.y + Size.y );
		if ( S.PreviewTarget.IsValid() )
		{
			DrawList->AddImage( NDebugUI::GetImGuiTexture( S.PreviewTarget->GetImageHandle().Value ), Origin, ImVec2( Origin.x + static_cast<float>( S.PreviewWidth ), Origin.y + static_cast<float>( S.PreviewHeight ) ) );
		}

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
		if ( ImGui::IsItemHovered() && IO.MouseWheel != 0.0f )
		{
			S.Distance = NMath::Clamp( S.Distance * ( IO.MouseWheel > 0.0f ? 0.88f : 1.0f / 0.88f ), 0.01f, 100000.0f );
		}

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
			NCString::Format( Info, sizeof( Info ), "%s   %.0f%%   %s", *Clip->Name, S.Animator.GetNormalizedTime() * 100.0f, S.Animator.IsFinished() ? "finished" : ( S.bPlaying ? "playing" : "paused" ) );
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
}
