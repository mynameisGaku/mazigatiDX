// SPDX-License-Identifier: MIT
#include "SoundEditorInternal.h"

namespace NSoundEditorUI
{
	float32 ToDb( float32 Linear )
	{
		return Linear > 0.0f ? DbPerLn * NMath::Log( Linear ) : -120.0f;
	}

	float32 FromDb( float32 Db )
	{
		return NMath::Pow( 10.0f, Db / 20.0f );
	}

	void SetStatus( FSoundEditorState& S, const FString& Message, bool bError )
	{
		S.StatusMessage = Message;
		S.bStatusIsError = bError;
	}

	bool HasSelection( const FSoundEditorState& S )
	{
		return S.SelectionEnd > S.SelectionStart;
	}

	// 加工の対象: 選んだ範囲（無ければ全体）
	FFrameRange TargetRange( const FSoundEditorState& S )
	{
		FFrameRange Range;
		const int64 Frames = S.Pcm.GetFrameCount();
		if ( HasSelection( S ) )
		{
			Range.Start = NMath::Clamp<int64>( S.SelectionStart, 0, Frames );
			Range.End = NMath::Clamp<int64>( S.SelectionEnd, 0, Frames );
		}
		else
		{
			Range.End = Frames;
		}
		return Range;
	}

	int64 MillisecondsToFrames( const FSoundEditorState& S, float32 Milliseconds )
	{
		return static_cast<int64>( NMath::Max( Milliseconds, 0.0f ) * 0.001f * static_cast<float32>( S.Pcm.SampleRate ) );
	}

	FString FormatSeconds( const FSoundEditorState& S, int64 Frames )
	{
		char Buffer[32];
		NCString::Format( Buffer, sizeof( Buffer ), "%.3f s", static_cast<float64>( Frames ) / static_cast<float64>( NMath::Max( S.Pcm.SampleRate, 1 ) ) );
		return FString( Buffer );
	}

	// 音が変わったあとの後片付け: 山谷・割れた所を作り直し、見えている範囲と選択を、音の長さに収める
	void Refresh( FSoundEditorState& S )
	{
		S.Peaks.Build( S.Pcm );
		S.Clipped = NPcmEdit::FindClippedRanges( S.Pcm, S.ClipThreshold );
		S.Silences.Reset();
		const int64 Frames = S.Pcm.GetFrameCount();
		S.CursorFrame = NMath::Clamp<int64>( S.CursorFrame, 0, Frames );
		if ( S.ViewFrames <= 0 || S.ViewFrames > Frames )
		{
			S.ViewFrames = Frames;
		}
		S.ViewStart = NMath::Clamp<int64>( S.ViewStart, 0, NMath::Max<int64>( Frames - S.ViewFrames, 0 ) );
		S.SelectionStart = NMath::Clamp<int64>( S.SelectionStart, 0, Frames );
		S.SelectionEnd = NMath::Clamp<int64>( S.SelectionEnd, 0, Frames );
	}

	void StopPreview( FSoundEditorState& S )
	{
		if ( S.PreviewHandle.IsValid() )
		{
			if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
			{
				Audio->Stop( S.PreviewHandle );
			}
			S.PreviewHandle = FSoundHandle();
		}
	}

	// 加工の前に、元に戻せるように保存する
	void PushUndo( FSoundEditorState& S )
	{
		S.UndoStack.Add( S.Pcm );
		if ( S.UndoStack.Num() > MaxUndo )
		{
			S.UndoStack.RemoveAt( 0 );
		}
		S.RedoStack.Reset();
	}

	// 選んだ範囲を削る（前後を、短くクロスフェードでつなぐ）
	void DeleteSelection( FSoundEditorState& S )
	{
		if ( !HasSelection( S ) )
		{
			SetStatus( S, "Select a range first.", true );
			return;
		}
		const int64 Fade = MillisecondsToFrames( S, S.DeleteCrossfadeMilliseconds );
		const FFrameRange Range = TargetRange( S );
		FPcmBuffer Result;
		if ( !NPcmEdit::Remove( S.Pcm, Range.Start, Range.End, Fade, Result ) )
		{
			SetStatus( S, "Delete: the whole sound cannot be deleted.", true );
			return;
		}
		PushUndo( S );
		const int64 CursorBefore = S.CursorFrame;
		S.Pcm = Result;
		S.SelectionStart = 0;
		S.SelectionEnd = 0;
		// 開始位置は、削った範囲より後ろなら、その分だけ前へ。範囲の中なら、削った所（つなぎ目）へ
		S.CursorFrame = CursorBefore >= Range.End ? CursorBefore - Range.GetLength() : NMath::Min( CursorBefore, Range.Start );
		S.bDirty = true;
		Refresh( S );
		SetStatus( S, "Deleted " + FormatSeconds( S, Range.GetLength() ) + ".", false );
	}

	// 選んだ範囲を、ループ区間にする（WAV の smpl として保存される）
	void SetLoopFromSelection( FSoundEditorState& S )
	{
		if ( !HasSelection( S ) )
		{
			SetStatus( S, "Select a range first.", true );
			return;
		}
		PushUndo( S );
		S.Pcm.LoopStartFrame = S.SelectionStart;
		S.Pcm.LoopEndFrame = S.SelectionEnd;
		S.bDirty = true;
		SetStatus( S, "Loop set: " + FormatSeconds( S, S.SelectionStart ) + " - " + FormatSeconds( S, S.SelectionEnd ) + " (saved in the WAV)", false );
	}

	void ClearLoopRegion( FSoundEditorState& S )
	{
		if ( !S.Pcm.HasLoop() )
		{
			return;
		}
		PushUndo( S );
		S.Pcm.ClearLoop();
		S.bDirty = true;
		SetStatus( S, "Loop cleared.", false );
	}

	// ループの聞こえ方: 全体を、ループ区間つきで鳴らす（開始位置があれば、そこから）
	void PreviewLoop( FSoundEditorState& S )
	{
		StopPreview( S );
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
		if ( Assets == nullptr || Audio == nullptr || !S.Pcm.HasLoop() )
		{
			SetStatus( S, "Set a loop first.", true );
			return;
		}
		CPcmSoundLoader Loader( Assets->GetBackend() );
		const TSharedPtr<CSound> Sound = Loader.CreateFromPcm( "SoundEditorLoopPreview", S.Pcm );
		if ( !Sound.IsValid() )
		{
			SetStatus( S, "The sound could not be played.", true );
			return;
		}
		const float32 Rate = static_cast<float32>( S.Pcm.SampleRate );
		Sound->SetLoopRegion( static_cast<float32>( S.Pcm.LoopStartFrame ) / Rate, static_cast<float32>( S.Pcm.LoopEndFrame ) / Rate );
		FSoundPlayParams Params;
		Params.Volume = NMath::Clamp( S.PreviewVolume, 0.0f, 1.0f );
		Params.bLoop = true;
		Params.StartTime = S.bHasCursor ? static_cast<float32>( S.CursorFrame ) / Rate : 0.0f;
		S.PreviewHandle = Audio->Play( Sound, Params );
	}

	void FindSilences( FSoundEditorState& S )
	{
		const FFrameRange Range = TargetRange( S );
		FPcmBuffer Part;
		S.Silences.Reset();
		if ( !NPcmEdit::Slice( S.Pcm, Range.Start, Range.End, Part ) )
		{
			return;
		}
		const TArray<FFrameRange> InPart = NPcmEdit::FindSilentRanges( Part, FromDb( S.SilenceThresholdDb ), MillisecondsToFrames( S, S.SilenceMinMilliseconds ) );
		for ( const FFrameRange& Silence : InPart )
		{
			FFrameRange Moved;
			Moved.Start = Silence.Start + Range.Start;
			Moved.End = Silence.End + Range.Start;
			S.Silences.Add( Moved );
		}
		SetStatus( S, FString::FromInt( S.Silences.Num() ) + " silent range(s) found.", false );
	}

	void ShortenSilences( FSoundEditorState& S )
	{
		const float32 Threshold = FromDb( S.SilenceThresholdDb );
		const int64 MinFrames = MillisecondsToFrames( S, S.SilenceMinMilliseconds );
		const int64 KeepFrames = MillisecondsToFrames( S, S.SilenceKeepMilliseconds );
		int64 Removed = 0;
		if ( ReplaceTarget( S, "Shorten silences", [&]( const FPcmBuffer& Part, FPcmBuffer& Made ) { return NPcmEdit::CollapseSilence( Part, Threshold, MinFrames, KeepFrames, Made, &Removed ); } ) )
		{
			SetStatus( S, "Shortened silences: removed " + FormatSeconds( S, Removed ) + ".", false );
		}
		else
		{
			SetStatus( S, "No silence is longer than the minimum length.", true );
		}
	}

	void Undo( FSoundEditorState& S )
	{
		if ( S.UndoStack.Num() == 0 )
		{
			return;
		}
		S.RedoStack.Add( S.Pcm );
		S.Pcm = S.UndoStack[S.UndoStack.Num() - 1];
		S.UndoStack.RemoveAt( S.UndoStack.Num() - 1 );
		S.bDirty = true;
		Refresh( S );
	}

	void Redo( FSoundEditorState& S )
	{
		if ( S.RedoStack.Num() == 0 )
		{
			return;
		}
		S.UndoStack.Add( S.Pcm );
		S.Pcm = S.RedoStack[S.RedoStack.Num() - 1];
		S.RedoStack.RemoveAt( S.RedoStack.Num() - 1 );
		S.bDirty = true;
		Refresh( S );
	}

	void TrimToSelection( FSoundEditorState& S )
	{
		FPcmBuffer Cut;
		if ( !HasSelection( S ) || !NPcmEdit::Slice( S.Pcm, S.SelectionStart, S.SelectionEnd, Cut ) )
		{
			SetStatus( S, "Select a range first.", true );
			return;
		}
		PushUndo( S );
		S.Pcm = Cut;
		S.SelectionStart = 0;
		S.SelectionEnd = 0;
		S.ViewStart = 0;
		S.ViewFrames = 0;
		S.bDirty = true;
		Refresh( S );
		SetStatus( S, "Trimmed to the selection.", false );
	}

	void SelectAudible( FSoundEditorState& S )
	{
		const FFrameRange Range = NPcmEdit::FindAudibleRange( S.Pcm );
		if ( Range.IsEmpty() )
		{
			SetStatus( S, "The sound is silent.", true );
			return;
		}
		S.SelectionStart = Range.Start;
		S.SelectionEnd = Range.End;
	}

	void SnapSelection( FSoundEditorState& S )
	{
		if ( !HasSelection( S ) )
		{
			SetStatus( S, "Select a range first.", true );
			return;
		}
		const int64 Distance = MillisecondsToFrames( S, S.SnapMilliseconds );
		const int64 Start = NPcmEdit::SnapToZeroCrossing( S.Pcm, S.SelectionStart, Distance );
		const int64 End = NPcmEdit::SnapToZeroCrossing( S.Pcm, S.SelectionEnd, Distance );
		if ( End > Start )
		{
			S.SelectionStart = Start;
			S.SelectionEnd = End;
		}
	}

	void Preview( FSoundEditorState& S )
	{
		StopPreview( S );
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
		if ( Assets == nullptr || Audio == nullptr )
		{
			SetStatus( S, "The audio system is not running.", true );
			return;
		}
		FFrameRange Range = TargetRange( S );
		if ( !HasSelection( S ) && S.bHasCursor )
		{
			Range.Start = NMath::Clamp<int64>( S.CursorFrame, 0, S.Pcm.GetFrameCount() );
		}
		FPcmBuffer Part;
		if ( !NPcmEdit::Slice( S.Pcm, Range.Start, Range.End, Part ) )
		{
			SetStatus( S, "Nothing to play from here (the start position is at the end).", true );
			return;
		}
		CPcmSoundLoader Loader( Assets->GetBackend() );
		const TSharedPtr<CSound> Sound = Loader.CreateFromPcm( "SoundEditorPreview", Part );
		if ( !Sound.IsValid() )
		{
			SetStatus( S, "The sound could not be played.", true );
			return;
		}
		FSoundPlayParams Params;
		Params.Volume = NMath::Clamp( S.PreviewVolume, 0.0f, 1.0f );
		Params.bLoop = S.bPreviewLoop;
		S.PreviewHandle = Audio->Play( Sound, Params );
	}

	bool Save( FSoundEditorState& S )
	{
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		if ( Assets == nullptr || !S.Pcm.IsValid() )
		{
			return false;
		}
		FString Path = S.SaveAsPath.Replace( "\\", "/" );
		if ( Path.IsEmpty() || Path.Contains( ":" ) || Path.StartsWith( "/" ) || Path.Contains( ".." ) )
		{
			SetStatus( S, "Give a path relative to the asset root (no drive letters, no '..').", true );
			return false;
		}
		if ( !Path.EndsWith( ".wav", ESearchCase::IgnoreCase ) )
		{
			Path += ".wav";
		}
		const FString Full = Assets->GetRootDirectory() / Path;
		if ( NFileSystem::FileExists( Full ) && !S.bOverwrite )
		{
			SetStatus( S, "'" + Path + "' already exists. Check Overwrite to replace it.", true );
			return false;
		}
		const int32 Slash = Full.FindLast( "/" );
		if ( Slash > 0 )
		{
			NFileSystem::EnsureDirectory( Full.Left( Slash ) );
		}
		if ( !CPcmSoundLoader::SaveWav( S.Pcm, Full ) )
		{
			SetStatus( S, "The file could not be written: " + Path, true );
			return false;
		}
		S.bDirty = false;
		S.bOverwrite = false;
		S.LastScanTime = -100.0;
		SetStatus( S, "Saved " + Path + ( S.Pcm.HasLoop() ? " (with the loop)" : "" ) + " (the original file is unchanged).", false );
		return true;
	}

	FString FormatTime( float64 Seconds )
	{
		char Buffer[32];
		if ( Seconds >= 60.0 )
		{
			NCString::Format( Buffer, sizeof( Buffer ), "%d:%06.3f", static_cast<int32>( Seconds / 60.0 ), Seconds - 60.0 * static_cast<float64>( static_cast<int32>( Seconds / 60.0 ) ) );
		}
		else
		{
			NCString::Format( Buffer, sizeof( Buffer ), "%.3f s", Seconds );
		}
		return FString( Buffer );
	}

	void RescanFiles( FSoundEditorState& S )
	{
		const double Now = ImGui::GetTime();
		if ( Now - S.LastScanTime < 3.0 )
		{
			return;
		}
		S.LastScanTime = Now;
		if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
		{
			S.SoundFiles = NFileSystem::ListFilesRecursive( Assets->GetRootDirectory(), Assets->GetReadableSoundExtensions() );
		}
	}
}
