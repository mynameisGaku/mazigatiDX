// SPDX-License-Identifier: MIT
#include "SoundEditorInternal.h"

namespace NSoundEditorUI
{
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
