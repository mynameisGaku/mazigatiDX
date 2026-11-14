#pragma once
#include "ModelAnimationEditor.h"
#include "Library/DebugUI/Common/EditorLog.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Platform/System/FileSystem.h"

//=============================================================================
// モデルアニメーションのエディタの中で、ファイルをまたいで使うもの（ModelAnimationEditor*.cpp 専用）
//=============================================================================
namespace NModelAnimationEditorUI
{
	using FState = FModelAnimationEditorState;
	inline constexpr const char* DefinitionExtension = ".gxmodelanim";
	inline CAssetSubsystem* GetAssets() { return GetAppSubsystem<CAssetSubsystem>(); }

	// ---- ModelAnimationEditor.cpp ----
	void SetStatus( FState& S, const FString& Text, bool bError = false );

	inline FString NormalizePath( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	// ---- ModelAnimationEditor.cpp ----
	void RescanFiles( FState& S, bool bForce );
	const FModelClipDef* SelectedClip( const FState& S );
	TSharedPtr<CModel> GetEffectiveMeshModel( const FState& S );

	// ---- ModelAnimationEditorPreview.cpp ----
	void ComputeBounds( FState& S );
	void EnsureScreen( FState& S, int32 Width, int32 Height );
	bool SampleMotionRange( FState& S, FVector3& OutMin, FVector3& OutMax );
	void FitCamera( FState& S );
	void PlaySelected( FState& S, bool bKeepTime );
	void SyncPreview( FState& S );

	// ---- ModelAnimationEditor.cpp ----
	void ResetSelection( FState& S );
	bool SaveCurrent( FState& S, const FString& AssetPath );
	void RequestAction( FState& S, FState::EPending Action, const FString& Path );

	// ---- ModelAnimationEditorPreview.cpp ----
	void RenderPreview( FState& S, int32 Width, int32 Height );
	void DrawViewport( FState& S );

	// ---- ModelAnimationEditorTimeline.cpp ----
	void DrawTimeline( FState& S );

	// ---- ModelAnimationEditorTabs.cpp ----
	void DrawModelTab( FState& S );
	void DrawClipsTab( FState& S );

	// ---- ModelAnimationEditorMenu.cpp ----
	void DrawMenuBar( FState& S, bool* pOpen );
}
