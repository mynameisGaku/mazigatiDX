// SPDX-License-Identifier: MIT
#include "SoundEditor.h"
#include "SoundEditorInternal.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Sound/PcmSoundLoader.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/FileSystem.h"
#include "VendorModules/ImGui/imgui.h"

using namespace NSoundEditorUI;


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
