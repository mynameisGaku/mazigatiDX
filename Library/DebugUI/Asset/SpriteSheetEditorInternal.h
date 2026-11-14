#pragma once
#include "SpriteSheetEditor.h"
#include "Library/DebugUI/Common/EditorLog.h"
#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/FileSystem.h"

//=============================================================================
// スプライトシートのエディタの中で、ファイルをまたいで使うもの（SpriteSheetEditor*.cpp 専用）
//=============================================================================
namespace NSpriteSheetEditorUI
{
	using FState = FSpriteSheetEditorState;
	inline constexpr const char* SheetExtension = ".gxsprite";
	inline CAssetSubsystem* GetAssets() { return GetAppSubsystem<CAssetSubsystem>(); }

	// ---- SpriteSheetEditor.cpp ----
	void SetStatus( FState& S, const FString& Text, bool bError = false );
	void RescanFiles( FState& S, bool bForce );

	//=========================================================================
	// 編集中の定義と、プレビュー用のスプライトシート・アニメーターを揃える
	//=========================================================================
	inline int32 TextureWidth( const FState& S ) { return S.Sheet.IsValid() && S.Sheet->GetTexture().IsValid() ? S.Sheet->GetTexture()->GetWidth() : 0; }
	inline int32 TextureHeight( const FState& S ) { return S.Sheet.IsValid() && S.Sheet->GetTexture().IsValid() ? S.Sheet->GetTexture()->GetHeight() : 0; }

	// ---- SpriteSheetEditor.cpp ----
	void RestartPreview( FState& S, bool bKeepTime );
	void SyncSheet( FState& S );

	//=========================================================================
	// 開く・保存
	//=========================================================================
	inline FString NormalizePath( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	// ---- SpriteSheetEditor.cpp ----
	void ResetSelection( FState& S );
	bool SaveCurrent( FState& S, const FString& AssetPath );
	void RequestAction( FState& S, FState::EPending Action, const FString& Path );

	//=========================================================================
	// 画像の上に重ねる枠
	//=========================================================================
	struct FOverlayFrame
	{
		FSpriteFrame Frame;
		int32 Index = 0;
		int32 GridIndex = INDEX_NONE;   // 格子のコマなら、その格子の番号
		bool bFree = false;
		bool bOutside = false;
	};

	// ---- SpriteSheetEditorCanvas.cpp ----
	bool IsOutside( const FSpriteFrame& Frame, int32 TexW, int32 TexH );
	void BuildOverlay( const FSpriteSheetDefinition& Definition, int32 TexW, int32 TexH, TArray<FOverlayFrame>& Out );
	ImU32 GridColor( int32 GridIndex, int32 Alpha );
	void DrawCanvas( FState& S );

	// ---- SpriteSheetEditorTabs.cpp ----
	void DrawSourceTab( FState& S );
	void DrawGridTab( FState& S );
	void DrawFramesTab( FState& S );
	void DrawClipsTab( FState& S );

	// ---- SpriteSheetEditorPreview.cpp ----
	void DrawPreview( FState& S, float DeltaTime );

	// ---- SpriteSheetEditorTimeline.cpp ----
	void DrawTimeline( FState& S );

	// ---- SpriteSheetEditorMenu.cpp ----
	void DrawMenuBar( FState& S, bool* pOpen );
}
