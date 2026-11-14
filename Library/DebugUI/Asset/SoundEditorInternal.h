// SPDX-License-Identifier: MIT
#pragma once
#include "SoundEditor.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Sound/PcmSoundLoader.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/FileSystem.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// サウンドエディタの中で、ファイルをまたいで使うもの（SoundEditor*.cpp 専用）
//=============================================================================
namespace NSoundEditorUI
{
	inline constexpr int32 MaxUndo = 16;
	inline constexpr float32 DbPerLn = 8.685889638f;   // 20 / ln( 10 )

	// ---- SoundEditorOperations.cpp ----
	float32 ToDb( float32 Linear );
	float32 FromDb( float32 Db );
	void SetStatus( FSoundEditorState& S, const FString& Message, bool bError );
	bool HasSelection( const FSoundEditorState& S );
	FFrameRange TargetRange( const FSoundEditorState& S );
	int64 MillisecondsToFrames( const FSoundEditorState& S, float32 Milliseconds );
	FString FormatSeconds( const FSoundEditorState& S, int64 Frames );
	void Refresh( FSoundEditorState& S );
	void StopPreview( FSoundEditorState& S );
	void PushUndo( FSoundEditorState& S );

	// 選んだ範囲（無ければ全体）を取り出して、Edit で加工して、元の場所へ書き戻す
	template<typename TEdit>
	void EditTarget( FSoundEditorState& S, const char* Name, TEdit Edit )
	{
		const FFrameRange Range = TargetRange( S );
		FPcmBuffer Part;
		if ( !NPcmEdit::Slice( S.Pcm, Range.Start, Range.End, Part ) )
		{
			SetStatus( S, "Nothing to edit.", true );
			return;
		}
		PushUndo( S );
		if ( !Edit( Part ) )
		{
			S.UndoStack.RemoveAt( S.UndoStack.Num() - 1 );
			SetStatus( S, FString( Name ) + ": nothing changed (the sound is silent?).", true );
			return;
		}
		for ( int32 Index = 0; Index < Part.Samples.Num(); ++Index )
		{
			S.Pcm.Samples[static_cast<int32>( Range.Start * S.Pcm.Channels ) + Index] = Part.Samples[Index];
		}
		S.bDirty = true;
		Refresh( S );
		SetStatus( S, FString( Name ) + ( HasSelection( S ) ? " (selection)" : " (whole sound)" ), false );
	}

	// 選んだ範囲（無ければ全体）を、Make が作った別の長さの音に置き換える。Make( 取り出した部分, 結果 ) は、できなければ false
	template<typename TMake>
	bool ReplaceTarget( FSoundEditorState& S, const char* Name, TMake Make )
	{
		const FFrameRange Range = TargetRange( S );
		FPcmBuffer Part;
		FPcmBuffer Made;
		if ( !NPcmEdit::Slice( S.Pcm, Range.Start, Range.End, Part ) || !Make( Part, Made ) )
		{
			SetStatus( S, FString( Name ) + ": nothing changed.", true );
			return false;
		}
		FPcmBuffer Result;
		if ( !NPcmEdit::Replace( S.Pcm, Range.Start, Range.End, Made, Result ) )
		{
			SetStatus( S, FString( Name ) + ": nothing changed.", true );
			return false;
		}
		PushUndo( S );
		const bool bHadSelection = HasSelection( S );
		S.Pcm = Result;
		// 選択は、置き換えた部分に（選んでいなかったら、そのまま選ばない）
		S.SelectionStart = bHadSelection ? Range.Start : 0;
		S.SelectionEnd = bHadSelection ? Range.Start + Made.GetFrameCount() : 0;
		S.bDirty = true;
		Refresh( S );
		return true;
	}

	// ---- SoundEditorOperations.cpp ----
	void DeleteSelection( FSoundEditorState& S );
	void SetLoopFromSelection( FSoundEditorState& S );
	void ClearLoopRegion( FSoundEditorState& S );
	void PreviewLoop( FSoundEditorState& S );
	void FindSilences( FSoundEditorState& S );
	void ShortenSilences( FSoundEditorState& S );
	void Undo( FSoundEditorState& S );
	void Redo( FSoundEditorState& S );
	void TrimToSelection( FSoundEditorState& S );
	void SelectAudible( FSoundEditorState& S );
	void SnapSelection( FSoundEditorState& S );
	void Preview( FSoundEditorState& S );
	bool Save( FSoundEditorState& S );
	FString FormatTime( float64 Seconds );
	void RescanFiles( FSoundEditorState& S );

	// ---- SoundEditorWaveform.cpp ----
	void DrawWaveform( FSoundEditorState& S );
}
