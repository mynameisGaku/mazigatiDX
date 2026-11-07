// SPDX-License-Identifier: MIT
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

namespace
{
	constexpr int32 MaxUndo = 16;
	constexpr float32 DbPerLn = 8.685889638f;   // 20 / ln( 10 )

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

	// 波形の表示と、ホイール・ドラッグの操作
	void DrawWaveform( FSoundEditorState& S )
	{
		const int64 Frames = S.Pcm.GetFrameCount();
		const float32 Width = NMath::Max( ImGui::GetContentRegionAvail().x, 100.0f );
		const float32 RulerHeight = 18.0f;
		const float32 ChannelHeight = NMath::Clamp( 260.0f / static_cast<float32>( S.Pcm.Channels ), 60.0f, 200.0f );
		const float32 Height = RulerHeight + ChannelHeight * static_cast<float32>( S.Pcm.Channels );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##waveform", ImVec2( Width, Height ), ImGuiButtonFlags_MouseButtonLeft );
		const bool bHovered = ImGui::IsItemHovered();
		ImGuiIO& IO = ImGui::GetIO();
		const auto FrameAtX = [&]( float32 X )
		{
			const float64 T = static_cast<float64>( NMath::Clamp( ( X - Origin.x ) / Width, 0.0f, 1.0f ) );
			return NMath::Clamp<int64>( S.ViewStart + static_cast<int64>( T * static_cast<float64>( S.ViewFrames ) + 0.5 ), 0, Frames );
		};

		// ホイール: 横へ動かす。Ctrl + ホイール: カーソルを中心に拡大縮小
		if ( bHovered && IO.MouseWheel != 0.0f )
		{
			if ( IO.KeyCtrl )
			{
				const int64 Anchor = FrameAtX( IO.MousePos.x );
				const float64 Ratio = static_cast<float64>( Anchor - S.ViewStart ) / static_cast<float64>( NMath::Max<int64>( S.ViewFrames, 1 ) );
				const float64 Scale = IO.MouseWheel > 0.0f ? 0.8 : 1.25;
				const int64 NewFrames = NMath::Clamp<int64>( static_cast<int64>( static_cast<float64>( S.ViewFrames ) * Scale ), NMath::Min<int64>( 16, Frames ), Frames );
				S.ViewStart = Anchor - static_cast<int64>( Ratio * static_cast<float64>( NewFrames ) );
				S.ViewFrames = NewFrames;
			}
			else
			{
				S.ViewStart -= static_cast<int64>( IO.MouseWheel * static_cast<float32>( S.ViewFrames ) * 0.1f );
			}
			S.ViewStart = NMath::Clamp<int64>( S.ViewStart, 0, NMath::Max<int64>( Frames - S.ViewFrames, 0 ) );
		}

		// 左ドラッグ: 範囲を選ぶ。クリックだけなら、選択を外す
		if ( ImGui::IsItemActivated() )
		{
			if ( IO.MousePos.y < Origin.y + RulerHeight )
			{
				// 目盛りの上: 再生の開始位置（ドラッグで動かせる）
				S.bDraggingCursor = true;
				S.bHasCursor = true;
				S.CursorFrame = FrameAtX( IO.MousePos.x );
			}
			else
			{
				S.DragAnchor = FrameAtX( IO.MousePos.x );
				S.bDragging = true;
				S.SelectionStart = S.DragAnchor;
				S.SelectionEnd = S.DragAnchor;
			}
		}
		if ( S.bDraggingCursor && ImGui::IsItemActive() )
		{
			S.CursorFrame = FrameAtX( IO.MousePos.x );
		}
		if ( S.bDraggingCursor && !ImGui::IsItemActive() )
		{
			S.bDraggingCursor = false;
		}
		if ( S.bDragging && ImGui::IsItemActive() )
		{
			const int64 Here = FrameAtX( IO.MousePos.x );
			S.SelectionStart = NMath::Min( S.DragAnchor, Here );
			S.SelectionEnd = NMath::Max( S.DragAnchor, Here );
		}
		if ( S.bDragging && !ImGui::IsItemActive() )
		{
			S.bDragging = false;
		}

		ImDrawList* Draw = ImGui::GetWindowDrawList();
		const ImVec2 End( Origin.x + Width, Origin.y + Height );
		Draw->AddRectFilled( Origin, End, IM_COL32( 22, 24, 30, 255 ) );

		// 目盛り（80 ピクセル以上あくように、きりのいい間隔）
		const float64 SecondsPerPixel = static_cast<float64>( S.ViewFrames ) / static_cast<float64>( S.Pcm.SampleRate ) / static_cast<float64>( Width );
		float64 Step = 0.001;
		const float64 Steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
		for ( const float64 Candidate : Steps )
		{
			Step = Candidate;
			if ( Candidate / SecondsPerPixel >= 80.0 )
			{
				break;
			}
		}
		const float64 ViewStartSeconds = static_cast<float64>( S.ViewStart ) / static_cast<float64>( S.Pcm.SampleRate );
		const float64 ViewEndSeconds = ViewStartSeconds + static_cast<float64>( S.ViewFrames ) / static_cast<float64>( S.Pcm.SampleRate );
		for ( float64 Tick = NMath::Ceil( ViewStartSeconds / Step ) * Step; Tick <= ViewEndSeconds; Tick += Step )
		{
			const float32 X = Origin.x + static_cast<float32>( ( Tick - ViewStartSeconds ) / SecondsPerPixel );
			Draw->AddLine( ImVec2( X, Origin.y ), ImVec2( X, End.y ), IM_COL32( 255, 255, 255, 18 ) );
			const FString Label = FormatTime( Tick );
			Draw->AddText( ImVec2( X + 3.0f, Origin.y + 2.0f ), IM_COL32( 170, 175, 190, 255 ), *Label );
		}

		// 選択
		if ( HasSelection( S ) )
		{
			const auto XOf = [&]( int64 Frame ) { return Origin.x + static_cast<float32>( static_cast<float64>( Frame - S.ViewStart ) / static_cast<float64>( S.ViewFrames ) * static_cast<float64>( Width ) ); };
			const float32 X0 = NMath::Clamp( XOf( S.SelectionStart ), Origin.x, End.x );
			const float32 X1 = NMath::Clamp( XOf( S.SelectionEnd ), Origin.x, End.x );
			if ( X1 > X0 )
			{
				Draw->AddRectFilled( ImVec2( X0, Origin.y + RulerHeight ), ImVec2( X1, End.y ), IM_COL32( 80, 140, 255, 55 ) );
				Draw->AddLine( ImVec2( X0, Origin.y + RulerHeight ), ImVec2( X0, End.y ), IM_COL32( 120, 170, 255, 255 ) );
				Draw->AddLine( ImVec2( X1, Origin.y + RulerHeight ), ImVec2( X1, End.y ), IM_COL32( 120, 170, 255, 255 ) );
			}
		}

		// 探した無音（灰色）
		if ( S.bShowSilences )
		{
			for ( const FFrameRange& Silence : S.Silences )
			{
				const float32 X0 = Origin.x + static_cast<float32>( static_cast<float64>( Silence.Start - S.ViewStart ) / static_cast<float64>( S.ViewFrames ) * static_cast<float64>( Width ) );
				const float32 X1 = Origin.x + static_cast<float32>( static_cast<float64>( Silence.End - S.ViewStart ) / static_cast<float64>( S.ViewFrames ) * static_cast<float64>( Width ) );
				const float32 Left = NMath::Clamp( X0, Origin.x, End.x );
				const float32 Right = NMath::Clamp( X1, Origin.x, End.x );
				if ( Right > Left )
				{
					Draw->AddRectFilled( ImVec2( Left, Origin.y + RulerHeight ), ImVec2( Right, End.y ), IM_COL32( 160, 160, 180, 45 ) );
				}
			}
		}

		// 波形（列ごとの最小・最大。割れた所は赤）
		const int32 NumColumns = NMath::Max( static_cast<int32>( Width ), 1 );
		TArray<FWaveformPeak> Columns;
		for ( int32 Channel = 0; Channel < S.Pcm.Channels; ++Channel )
		{
			const float32 Top = Origin.y + RulerHeight + ChannelHeight * static_cast<float32>( Channel );
			const float32 Middle = Top + ChannelHeight * 0.5f;
			const float32 Half = ChannelHeight * 0.5f - 3.0f;
			Draw->AddLine( ImVec2( Origin.x, Middle ), ImVec2( End.x, Middle ), IM_COL32( 255, 255, 255, 40 ) );
			if ( Channel > 0 )
			{
				Draw->AddLine( ImVec2( Origin.x, Top ), ImVec2( End.x, Top ), IM_COL32( 255, 255, 255, 60 ) );
			}
			S.Peaks.GetColumns( S.Pcm, S.ViewStart, S.ViewStart + S.ViewFrames, NumColumns, Channel, Columns );
			for ( int32 Column = 0; Column < Columns.Num(); ++Column )
			{
				const FWaveformPeak& Peak = Columns[Column];
				const float32 X = Origin.x + static_cast<float32>( Column ) * Width / static_cast<float32>( NumColumns ) + 0.5f;
				const float32 YTop = Middle - NMath::Clamp( Peak.Max, -1.0f, 1.0f ) * Half;
				const float32 YBottom = Middle - NMath::Clamp( Peak.Min, -1.0f, 1.0f ) * Half;
				const bool bClipped = S.bShowClipped && ( Peak.Max >= S.ClipThreshold || Peak.Min <= -S.ClipThreshold );
				Draw->AddLine( ImVec2( X, YTop ), ImVec2( X, NMath::Max( YBottom, YTop + 1.0f ) ), bClipped ? IM_COL32( 255, 80, 70, 255 ) : IM_COL32( 110, 200, 160, 255 ) );
			}
		}
		// ループ区間（緑の帯と線）
		if ( S.Pcm.HasLoop() )
		{
			const auto XOfLoop = [&]( int64 Frame ) { return Origin.x + static_cast<float32>( static_cast<float64>( Frame - S.ViewStart ) / static_cast<float64>( S.ViewFrames ) * static_cast<float64>( Width ) ); };
			const float32 X0 = XOfLoop( S.Pcm.LoopStartFrame );
			const float32 X1 = XOfLoop( S.Pcm.LoopEndFrame );
			const float32 Left = NMath::Clamp( X0, Origin.x, End.x );
			const float32 Right = NMath::Clamp( X1, Origin.x, End.x );
			if ( Right > Left )
			{
				Draw->AddRectFilled( ImVec2( Left, Origin.y + RulerHeight ), ImVec2( Right, End.y ), IM_COL32( 90, 210, 120, 28 ) );
			}
			if ( X0 >= Origin.x && X0 <= End.x )
			{
				Draw->AddLine( ImVec2( X0, Origin.y + RulerHeight ), ImVec2( X0, End.y ), IM_COL32( 90, 230, 130, 255 ), 1.5f );
				Draw->AddText( ImVec2( X0 + 3.0f, End.y - 16.0f ), IM_COL32( 90, 230, 130, 255 ), "loop" );
			}
			if ( X1 >= Origin.x && X1 <= End.x )
			{
				Draw->AddLine( ImVec2( X1, Origin.y + RulerHeight ), ImVec2( X1, End.y ), IM_COL32( 90, 230, 130, 255 ), 1.5f );
			}
		}

		// 再生の開始位置（黄色い線。目盛りをクリックすると動く）
		if ( S.bHasCursor && S.CursorFrame >= S.ViewStart && S.CursorFrame <= S.ViewStart + S.ViewFrames )
		{
			const float32 X = Origin.x + static_cast<float32>( static_cast<float64>( S.CursorFrame - S.ViewStart ) / static_cast<float64>( S.ViewFrames ) * static_cast<float64>( Width ) );
			Draw->AddLine( ImVec2( X, Origin.y ), ImVec2( X, End.y ), IM_COL32( 255, 220, 90, 255 ), 1.5f );
			Draw->AddTriangleFilled( ImVec2( X - 5.0f, Origin.y ), ImVec2( X + 5.0f, Origin.y ), ImVec2( X, Origin.y + 8.0f ), IM_COL32( 255, 220, 90, 255 ) );
		}
		Draw->AddRect( Origin, End, IM_COL32( 255, 255, 255, 50 ) );

		if ( bHovered )
		{
			const int64 Frame = FrameAtX( IO.MousePos.x );
			ImGui::SetTooltip( "%s  (frame %lld)", UI_TEXT( FormatTime( static_cast<float64>( Frame ) / static_cast<float64>( S.Pcm.SampleRate ) ) ), static_cast<long long>( Frame ) );
		}
	}
}

bool NDebugUI::OpenSoundFile( FSoundEditorState& S, const FString& AssetPath )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return false;
	}
	const FString Path = AssetPath.Replace( "\\", "/" );
	if ( S.bDirty && S.FilePath != Path )
	{
		SetStatus( S, "The sound has unsaved changes. Save it (or reopen the same file to discard them) first.", true );
		return false;
	}
	const TOptional<FString> Full = Assets->ResolvePath( Path );
	FPcmBuffer Pcm;
	CPcmSoundLoader Loader( Assets->GetBackend() );
	if ( !Full.IsSet() || !Loader.Decode( Full.Get( FString() ), Pcm ) || !Pcm.IsValid() )
	{
		SetStatus( S, "Could not read the sound: " + Path, true );
		return false;
	}
	StopPreview( S );
	S.Pcm = Pcm;
	S.FilePath = Path;
	S.bDirty = false;
	S.UndoStack.Reset();
	S.RedoStack.Reset();
	S.SelectionStart = 0;
	S.SelectionEnd = 0;
	S.ViewStart = 0;
	S.ViewFrames = 0;
	// 保存先の既定: 同じ場所の、_edit を付けた WAV
	const int32 Dot = Path.FindLast( "." );
	S.SaveAsPath = ( Dot > 0 ? Path.Left( Dot ) : Path ) + "_edit.wav";
	S.bOverwrite = false;
	Refresh( S );
	SetStatus( S, "Opened " + Path, false );
	return true;
}

void NDebugUI::DrawSoundEditor( FSoundEditorState& S, bool* pOpen )
{
	RescanFiles( S );
	ImGui::SetNextWindowSize( ImVec2( 1000.0f, 840.0f ), ImGuiCond_FirstUseEver );
	char Title[96];
	NCString::Format( Title, sizeof( Title ), "Sound Editor%s###SoundEditor", S.bDirty ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen ) )
	{
		ImGui::End();
		return;
	}
	// 開く
	ImGui::SetNextItemWidth( 360.0f );
	if ( NDebugUI::FileCombo( "##opensound", S.OpenSelection, S.SoundFiles, "(open a sound...)" ) && !S.OpenSelection.IsEmpty() )
	{
		OpenSoundFile( S, S.OpenSelection );
	}
	if ( !S.Pcm.IsValid() )
	{
		ImGui::SetNextItemWidth( 300.0f );
		NDebugUI::InputStringWithHint( "##openpath", "or a path (any format the backend can read)", S.OpenPath );
		ImGui::SameLine();
		if ( ImGui::Button( "Open path" ) && !S.OpenPath.IsEmpty() )
		{
			OpenSoundFile( S, S.OpenPath );
		}
		ImGui::TextDisabled( "Open a sound file (.wav, .mp3, .ogg, ... whatever the audio backend can read) from the asset root." );
		if ( !S.StatusMessage.IsEmpty() )
		{
			ImGui::TextColored( S.bStatusIsError ? ImVec4( 1.0f, 0.5f, 0.45f, 1.0f ) : ImVec4( 0.7f, 0.7f, 0.75f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
		}
		ImGui::End();
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "%s%s", UI_TEXT( S.FilePath ), S.bDirty ? " *" : "" );
	ImGui::SetNextItemWidth( 300.0f );
	NDebugUI::InputStringWithHint( "##openpath", "or a path (any format the backend can read)", S.OpenPath );
	ImGui::SameLine();
	if ( ImGui::Button( "Open path" ) && !S.OpenPath.IsEmpty() )
	{
		OpenSoundFile( S, S.OpenPath );
	}

	// 情報
	const float32 Peak = NPcmEdit::GetPeak( S.Pcm );
	if ( S.Pcm.HasLoop() )
	{
		ImGui::TextColored( ImVec4( 0.45f, 0.9f, 0.55f, 1.0f ), "Loop: %s - %s", UI_TEXT( FormatTime( static_cast<float64>( S.Pcm.LoopStartFrame ) / static_cast<float64>( S.Pcm.SampleRate ) ) ), UI_TEXT( FormatTime( static_cast<float64>( S.Pcm.LoopEndFrame ) / static_cast<float64>( S.Pcm.SampleRate ) ) ) );
		ImGui::SameLine();
	}
	ImGui::Text( "%lld frames, %d Hz, %d ch, %s   peak %.1f dB   clipped: %d range(s)", static_cast<long long>( S.Pcm.GetFrameCount() ), S.Pcm.SampleRate, S.Pcm.Channels,
				 UI_TEXT( FormatTime( static_cast<float64>( S.Pcm.GetLengthSeconds() ) ) ), ToDb( Peak ), S.Clipped.Num() );
	if ( HasSelection( S ) )
	{
		ImGui::Text( "Selection: %s - %s  (%s, %lld frames)", UI_TEXT( FormatTime( static_cast<float64>( S.SelectionStart ) / static_cast<float64>( S.Pcm.SampleRate ) ) ),
					 UI_TEXT( FormatTime( static_cast<float64>( S.SelectionEnd ) / static_cast<float64>( S.Pcm.SampleRate ) ) ),
					 UI_TEXT( FormatTime( static_cast<float64>( S.SelectionEnd - S.SelectionStart ) / static_cast<float64>( S.Pcm.SampleRate ) ) ), static_cast<long long>( S.SelectionEnd - S.SelectionStart ) );
	}
	else
	{
		ImGui::TextDisabled( "No selection: edits apply to the whole sound. Drag on the waveform to select. Wheel = scroll, Ctrl + wheel = zoom." );
	}

	DrawWaveform( S );

	// 選択の始まり・終わりと、再生の開始位置を、秒の数値でも指せる
	{
		const float64 Rate = static_cast<float64>( S.Pcm.SampleRate );
		const float64 Length = static_cast<float64>( S.Pcm.GetFrameCount() ) / Rate;
		float32 StartSeconds = static_cast<float32>( static_cast<float64>( S.SelectionStart ) / Rate );
		float32 EndSeconds = static_cast<float32>( static_cast<float64>( S.SelectionEnd ) / Rate );
		float32 CursorSeconds = static_cast<float32>( static_cast<float64>( S.CursorFrame ) / Rate );
		ImGui::SetNextItemWidth( 110.0f );
		if ( ImGui::InputFloat( "start##selection", &StartSeconds, 0.001f, 0.01f, "%.3f s" ) )
		{
			S.SelectionStart = NMath::Clamp<int64>( static_cast<int64>( NMath::Clamp( static_cast<float64>( StartSeconds ), 0.0, Length ) * Rate + 0.5 ), 0, S.Pcm.GetFrameCount() );
			S.SelectionEnd = NMath::Max( S.SelectionEnd, S.SelectionStart );
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 110.0f );
		if ( ImGui::InputFloat( "end##selection", &EndSeconds, 0.001f, 0.01f, "%.3f s" ) )
		{
			S.SelectionEnd = NMath::Clamp<int64>( static_cast<int64>( NMath::Clamp( static_cast<float64>( EndSeconds ), 0.0, Length ) * Rate + 0.5 ), 0, S.Pcm.GetFrameCount() );
			S.SelectionStart = NMath::Min( S.SelectionStart, S.SelectionEnd );
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 110.0f );
		if ( ImGui::InputFloat( "start position##cursor", &CursorSeconds, 0.001f, 0.01f, "%.3f s" ) )
		{
			S.CursorFrame = NMath::Clamp<int64>( static_cast<int64>( NMath::Clamp( static_cast<float64>( CursorSeconds ), 0.0, Length ) * Rate + 0.5 ), 0, S.Pcm.GetFrameCount() );
			S.bHasCursor = true;
		}
		if ( S.bHasCursor )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( "selection start = here" ) )
			{
				S.SelectionStart = S.CursorFrame;
				S.SelectionEnd = NMath::Max( S.SelectionEnd, S.SelectionStart );
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "selection end = here" ) )
			{
				S.SelectionEnd = S.CursorFrame;
				S.SelectionStart = NMath::Min( S.SelectionStart, S.SelectionEnd );
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "clear" ) )
			{
				S.bHasCursor = false;
				S.CursorFrame = 0;
			}
		}
	}

	// 表示と選択
	if ( ImGui::Button( "Fit" ) )
	{
		S.ViewStart = 0;
		S.ViewFrames = S.Pcm.GetFrameCount();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( !HasSelection( S ) );
	if ( ImGui::Button( "Zoom to selection" ) )
	{
		S.ViewStart = S.SelectionStart;
		S.ViewFrames = NMath::Max<int64>( S.SelectionEnd - S.SelectionStart, 16 );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Snap to zero crossings" ) )
	{
		SnapSelection( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 80.0f );
	ImGui::DragFloat( "ms##snap", &S.SnapMilliseconds, 0.1f, 0.0f, 100.0f, "%.1f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Select audible part" ) )
	{
		SelectAudible( S );
	}
	ImGui::SameLine();
	if ( ImGui::Checkbox( "Show clipped", &S.bShowClipped ) )
	{
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 90.0f );
	if ( ImGui::DragFloat( "threshold##clip", &S.ClipThreshold, 0.001f, 0.5f, 1.0f, "%.3f" ) )
	{
		S.Clipped = NPcmEdit::FindClippedRanges( S.Pcm, S.ClipThreshold );
	}

	// 加工
	ImGui::SeparatorText( "Edit" );
	ImGui::BeginDisabled( S.UndoStack.Num() == 0 );
	if ( ImGui::Button( "Undo" ) )
	{
		Undo( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( S.RedoStack.Num() == 0 );
	if ( ImGui::Button( "Redo" ) )
	{
		Redo( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !HasSelection( S ) );
	if ( ImGui::Button( "Trim to selection" ) )
	{
		TrimToSelection( S );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Delete selection" ) )
	{
		DeleteSelection( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 80.0f );
	ImGui::DragFloat( "ms##deletefade", &S.DeleteCrossfadeMilliseconds, 0.1f, 0.0f, 100.0f, "%.1f" );
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Crossfade at the cut, to avoid a click" );
	}

	ImGui::SetNextItemWidth( 90.0f );
	ImGui::DragFloat( "dB##normalize", &S.NormalizeTargetDb, 0.1f, -60.0f, 0.0f, "%.1f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Normalize" ) )
	{
		const float32 Target = FromDb( S.NormalizeTargetDb );
		EditTarget( S, "Normalize", [Target]( FPcmBuffer& Part ) { return NPcmEdit::Normalize( Part, Target ); } );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 120.0f );
	ImGui::InputFloat( "dB##gain", &S.GainDb, 0.1f, 1.0f, "%.2f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Apply gain" ) )
	{
		const float32 Gain = FromDb( S.GainDb );
		EditTarget( S, "Gain", [Gain]( FPcmBuffer& Part ) { NPcmEdit::ApplyGain( Part, Gain ); return true; } );
	}
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "%.2f dB = x%.3f (%.1f%%)", S.GainDb, FromDb( S.GainDb ), FromDb( S.GainDb ) * 100.0f );
	}

	// 範囲の頭から終わりへ、音量をなめらかに変える（フェードより自由に: 例 0 dB から -12 dB へ、または -20 dB から 0 dB へ）
	ImGui::SetNextItemWidth( 120.0f );
	ImGui::InputFloat( "from dB##ramp", &S.RampStartDb, 0.1f, 1.0f, "%.2f" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 120.0f );
	ImGui::InputFloat( "to dB##ramp", &S.RampEndDb, 0.1f, 1.0f, "%.2f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Gain ramp" ) )
	{
		const float32 StartGain = FromDb( S.RampStartDb );
		const float32 EndGain = FromDb( S.RampEndDb );
		EditTarget( S, "Gain ramp", [StartGain, EndGain]( FPcmBuffer& Part ) { NPcmEdit::ApplyGainRamp( Part, StartGain, EndGain, 0, -1, true ); return true; } );
	}

	ImGui::SetNextItemWidth( 90.0f );
	ImGui::DragFloat( "ms##fadein", &S.FadeInMilliseconds, 0.5f, 0.0f, 5000.0f, "%.1f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Fade in" ) )
	{
		const int64 Frames = MillisecondsToFrames( S, S.FadeInMilliseconds );
		const EPcmFadeCurve Curve = S.bSmoothFade ? EPcmFadeCurve::Smooth : EPcmFadeCurve::Linear;
		EditTarget( S, "Fade in", [Frames, Curve]( FPcmBuffer& Part ) { NPcmEdit::FadeIn( Part, Frames, Curve ); return true; } );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 90.0f );
	ImGui::DragFloat( "ms##fadeout", &S.FadeOutMilliseconds, 0.5f, 0.0f, 5000.0f, "%.1f" );
	ImGui::SameLine();
	if ( ImGui::Button( "Fade out" ) )
	{
		const int64 Frames = MillisecondsToFrames( S, S.FadeOutMilliseconds );
		const EPcmFadeCurve Curve = S.bSmoothFade ? EPcmFadeCurve::Smooth : EPcmFadeCurve::Linear;
		EditTarget( S, "Fade out", [Frames, Curve]( FPcmBuffer& Part ) { NPcmEdit::FadeOut( Part, Frames, Curve ); return true; } );
	}
	ImGui::SameLine();
	ImGui::Checkbox( "Smooth curve", &S.bSmoothFade );

	// 無音の部分
	ImGui::SeparatorText( "Silence" );
	ImGui::SetNextItemWidth( 100.0f );
	ImGui::DragFloat( "threshold dB", &S.SilenceThresholdDb, 0.5f, -96.0f, -10.0f, "%.1f" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 100.0f );
	ImGui::DragFloat( "min ms", &S.SilenceMinMilliseconds, 5.0f, 1.0f, 10000.0f, "%.0f" );
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Only silences at least this long are found / shortened" );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 100.0f );
	ImGui::DragFloat( "keep ms", &S.SilenceKeepMilliseconds, 5.0f, 0.0f, 10000.0f, "%.0f" );
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "A long silence is shortened to this length" );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Find silences" ) )
	{
		FindSilences( S );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Shorten silences" ) )
	{
		ShortenSilences( S );
	}
	ImGui::SameLine();
	ImGui::Checkbox( "Show silences", &S.bShowSilences );

	// ループ区間
	ImGui::SeparatorText( "Loop" );
	ImGui::BeginDisabled( !HasSelection( S ) );
	if ( ImGui::Button( "Set loop = selection" ) )
	{
		SetLoopFromSelection( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !S.Pcm.HasLoop() );
	if ( ImGui::Button( "Clear loop" ) )
	{
		ClearLoopRegion( S );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Play looped" ) )
	{
		PreviewLoop( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled( "(saved in the WAV's smpl chunk; the game applies it when the WAV is loaded)" );

	// 試し聞き
	ImGui::SeparatorText( "Preview" );
	if ( ImGui::Button( HasSelection( S ) ? "Play selection" : ( S.bHasCursor ? "Play from start position" : "Play" ) ) )
	{
		Preview( S );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Stop" ) )
	{
		StopPreview( S );
	}
	ImGui::SameLine();
	ImGui::Checkbox( "Loop", &S.bPreviewLoop );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 160.0f );
	ImGui::SliderFloat( "Volume", &S.PreviewVolume, 0.0f, 1.0f, "%.2f" );

	// 保存
	ImGui::SeparatorText( "Save" );
	ImGui::SetNextItemWidth( 360.0f );
	NDebugUI::InputStringWithHint( "##saveas", "path under the asset root (.wav)", S.SaveAsPath );
	ImGui::SameLine();
	ImGui::Checkbox( "Overwrite", &S.bOverwrite );
	ImGui::SameLine();
	if ( ImGui::Button( "Save as WAV" ) )
	{
		Save( S );
	}

	if ( !S.StatusMessage.IsEmpty() )
	{
		ImGui::TextColored( S.bStatusIsError ? ImVec4( 1.0f, 0.5f, 0.45f, 1.0f ) : ImVec4( 0.7f, 0.7f, 0.75f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
	}
	ImGui::End();
}
