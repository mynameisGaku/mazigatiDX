#include "AssetSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Animation/SpriteAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/DefinitionEditing.h"
#include "Library/Framework/Asset/FontFile.h"
#include "Library/Framework/Audio/AudioComponents.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"

using namespace NAssetTest;

namespace
{
	//=========================================================================
	// 1. 純粋な計算
	//=========================================================================
	void TestTimeline( FChecker& C )
	{
		C.Group = "timeline";

		// Loop: 0.1, 0.1, 0.2 秒の 3 コマ
		const TArray<float32> Durations { 0.1f, 0.1f, 0.2f };
		const FAnimTimeline Loop = FAnimTimeline::Build( Durations, EAnimLoopMode::Loop );
		C.Check( Loop.NumSteps() == 3, "loop: 3 steps" );
		C.Near( Loop.Total, 0.4, 1.0e-6, "loop total" );
		C.Check( NAnim::Locate( Loop, 0.0 ).Step == 0, "loop at 0.00" );
		C.Check( NAnim::Locate( Loop, 0.05 ).Step == 0, "loop at 0.05" );
		C.Check( NAnim::Locate( Loop, 0.15 ).Step == 1, "loop at 0.15" );
		C.Check( NAnim::Locate( Loop, 0.25 ).Step == 2, "loop at 0.25" );
		C.Check( NAnim::Locate( Loop, 0.39 ).Step == 2, "loop at 0.39" );
		C.Check( NAnim::Locate( Loop, 0.45 ).Step == 0 && NAnim::Locate( Loop, 0.45 ).AbsoluteStep == 3, "loop wraps to the next cycle (absolute step 3)" );
		C.Check( NAnim::Locate( Loop, 0.85 ).AbsoluteStep == 6, "loop second wrap (absolute step 6)" );
		C.Check( NAnim::Locate( Loop, -0.05 ).Step == 2 && NAnim::Locate( Loop, -0.05 ).AbsoluteStep == -1, "loop with a negative playhead" );

		// Once: 範囲に収める
		const FAnimTimeline Once = FAnimTimeline::Build( Durations, EAnimLoopMode::Once );
		C.Check( NAnim::Locate( Once, 5.0 ).Step == 2 && NAnim::Locate( Once, 5.0 ).AbsoluteStep == 2, "once clamps to the last step" );
		C.Check( NAnim::Locate( Once, -1.0 ).Step == 0, "once clamps to the first step" );

		// PingPong: 4 コマ -> 0 1 2 3 2 1（端は 1 回ずつ）
		const TArray<float32> Four { 0.1f, 0.1f, 0.1f, 0.1f };
		const FAnimTimeline PingPong = FAnimTimeline::Build( Four, EAnimLoopMode::PingPong );
		C.Check( PingPong.NumSteps() == 6, "ping-pong of 4 frames has 6 steps (got %d)", PingPong.NumSteps() );
		const int32 Expected[6] = { 0, 1, 2, 3, 2, 1 };
		for ( int32 i = 0; i < 6; ++i )
		{
			const FAnimLocation Location = NAnim::Locate( PingPong, 0.05 + 0.1 * i );
			C.Check( PingPong.StepSource[Location.Step] == Expected[i], "ping-pong step %d should show frame %d", i, Expected[i] );
		}
		C.Check( NAnim::Locate( PingPong, 0.65 ).Step == 0, "ping-pong wraps after 6 steps" );
		C.Check( FAnimTimeline::Build( TArray<float32> { 0.1f }, EAnimLoopMode::PingPong ).NumSteps() == 1, "ping-pong of 1 frame" );
		C.Check( FAnimTimeline::Build( TArray<float32> { 0.1f, 0.1f }, EAnimLoopMode::PingPong ).NumSteps() == 2, "ping-pong of 2 frames" );

		// 長さ 0 のコマ（下限が掛かる）でも固まらない
		const FAnimTimeline Zero = FAnimTimeline::Build( TArray<float32> { 0.0f, 0.0f }, EAnimLoopMode::Loop );
		C.Check( Zero.Total > 0.0f, "zero-length frames get a minimum duration" );

		// 連続した時間
		C.Near( NAnim::ToClipTime( 1.25, 1.0f, EAnimLoopMode::Loop ), 0.25, 1.0e-6, "continuous loop time" );
		C.Near( NAnim::ToClipTime( -0.25, 1.0f, EAnimLoopMode::Loop ), 0.75, 1.0e-6, "continuous loop time (negative)" );
		C.Near( NAnim::ToClipTime( 1.25, 1.0f, EAnimLoopMode::PingPong ), 0.75, 1.0e-6, "continuous ping-pong time" );
		C.Near( NAnim::ToClipTime( 7.0, 1.0f, EAnimLoopMode::Once ), 1.0, 1.0e-6, "continuous once time" );
		C.Near( NAnim::ToNormalized( 0.5, 2.0f, EAnimLoopMode::Loop ), 0.25, 1.0e-6, "normalized time" );

		C.Check( ParseLoopMode( "pingpong", EAnimLoopMode::Once ) == EAnimLoopMode::PingPong && ParseLoopMode( "LOOP", EAnimLoopMode::Once ) == EAnimLoopMode::Loop
					 && ParseLoopMode( "???", EAnimLoopMode::Once ) == EAnimLoopMode::Once,
				 "loop mode parsing" );
	}

	void TestEntered( FChecker& C )
	{
		C.Group = "events";

		auto Collect = []( int64 From, int64 To, int32 N ) {
			std::vector<int32> Steps;
			NAnim::ForEachEnteredStep( From, To, N, [&]( int32 Step ) { Steps.push_back( Step ); } );
			return Steps;
		};
		C.Check( Collect( 2, 5, 3 ) == std::vector<int32> { 0, 1, 2 }, "forward: absolute steps 3, 4, 5 -> 0, 1, 2" );
		C.Check( Collect( 5, 2, 3 ) == std::vector<int32> { 1, 0, 2 }, "backward: absolute steps 4, 3, 2 -> 1, 0, 2" );
		C.Check( Collect( 4, 4, 3 ).empty(), "no movement, no steps" );
		C.Check( Collect( -1, 1, 3 ) == std::vector<int32> { 0, 1 }, "crossing zero (steps 0, 1)" );
		C.Check( static_cast<int32>( Collect( 0, 1000000, 3 ).size() ) == NAnim::MaxEnumerated, "a very long jump is capped at MaxEnumerated" );

		// 通り過ぎた時刻の回数
		C.Check( NAnim::CountMarkerCrossings( 0.0, 2.6, 0.5f, 2.0f, EAnimLoopMode::Loop ) == 2, "loop marker crossed twice (0.5, 2.5)" );
		C.Check( NAnim::CountMarkerCrossings( 0.5, 2.5, 0.5f, 2.0f, EAnimLoopMode::Loop ) == 1, "forward range is (from, to]" );
		C.Check( NAnim::CountMarkerCrossings( 2.5, 0.4, 0.5f, 2.0f, EAnimLoopMode::Loop ) == 1, "backward range is [to, from)" );
		C.Check( NAnim::CountMarkerCrossings( 0.0, 1.0, 1.0f, 2.0f, EAnimLoopMode::Once ) == 1, "once marker at the end of the range counts" );
		C.Check( NAnim::CountMarkerCrossings( 1.0, 2.0, 1.0f, 2.0f, EAnimLoopMode::Once ) == 0, "once marker is not counted twice" );
		C.Check( NAnim::CountMarkerCrossings( 0.0, 4.0, 0.5f, 2.0f, EAnimLoopMode::PingPong ) == 2, "ping-pong marker is crossed going and coming back" );
		C.Check( NAnim::CountMarkerCrossings( -0.1, 0.1, 0.0f, 2.0f, EAnimLoopMode::PingPong ) == 1, "ping-pong marker at the start is counted once per cycle" );
		C.Check( NAnim::CountMarkerCrossings( 0.0, 1.0, 3.0f, 2.0f, EAnimLoopMode::Loop ) == 0, "a marker beyond the length is ignored" );
	}

	void TestAudioMath( FChecker& C )
	{
		C.Group = "audio-math";

		for ( const EFadeCurve Curve : { EFadeCurve::Linear, EFadeCurve::EaseIn, EFadeCurve::EaseOut, EFadeCurve::SmoothStep, EFadeCurve::EqualPower } )
		{
			C.Near( NAudio::ShapeFade( Curve, 0.0f, true ), 0.0, 1.0e-6, "fade starts at 0" );
			C.Near( NAudio::ShapeFade( Curve, 1.0f, true ), 1.0, 1.0e-6, "fade ends at 1" );
			float32 Previous = -1.0f;
			bool bMonotonic = true;
			for ( int32 i = 0; i <= 20; ++i )
			{
				const float32 Value = NAudio::ShapeFade( Curve, static_cast<float32>( i ) / 20.0f, true );
				bMonotonic = bMonotonic && Value >= Previous - 1.0e-6f;
				Previous = Value;
			}
			C.Check( bMonotonic, "fade curve %d is monotonic", static_cast<int32>( Curve ) );
		}
		C.Near( NAudio::ShapeFade( EFadeCurve::EaseIn, 0.5f, true ), 0.25, 1.0e-6, "ease-in(0.5)" );
		C.Near( NAudio::ShapeFade( EFadeCurve::SmoothStep, 0.5f, true ), 0.5, 1.0e-6, "smooth-step(0.5)" );

		// EqualPower: 上がる側と下がる側の 2 乗の和が一定（1）
		for ( int32 i = 0; i <= 10; ++i )
		{
			const float32 T = static_cast<float32>( i ) / 10.0f;
			const float32 In = NAudio::ShapeFade( EFadeCurve::EqualPower, T, true );
			const float32 Out = 1.0f - NAudio::ShapeFade( EFadeCurve::EqualPower, T, false );   // 1 から 0 に下がる値
			C.Near( In * In + Out * Out, 1.0, 1.0e-5, "equal-power crossfade keeps the power constant" );
		}

		// FAudioFade
		FAudioFade Fade;
		Fade.Reset( 0.0f );
		Fade.FadeTo( 1.0f, 2.0f );
		Fade.Step( 0.5f );
		C.Near( Fade.Value, 0.25, 1.0e-6, "fade 25%" );
		C.Check( Fade.IsFading(), "still fading" );
		Fade.Step( 5.0f );
		C.Near( Fade.Value, 1.0, 1.0e-6, "fade done" );
		C.Check( !Fade.IsFading(), "fade finished" );
		Fade.FadeTo( 0.0f, 0.0f );
		C.Near( Fade.Value, 0.0, 1.0e-6, "zero-length fade is immediate" );

		// 距離による減衰
		C.Near( NAudio::RolloffGain( EAudioRolloff::Linear, 0.5f, 1.0f, 11.0f ), 1.0, 1.0e-6, "inside the min distance" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Linear, 6.0f, 1.0f, 11.0f ), 0.5, 1.0e-6, "linear rolloff halfway" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Linear, 11.0f, 1.0f, 11.0f ), 0.0, 1.0e-6, "silent at the max distance" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Linear, 50.0f, 1.0f, 11.0f ), 0.0, 1.0e-6, "silent beyond the max distance" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Squared, 6.0f, 1.0f, 11.0f ), 0.25, 1.0e-6, "squared rolloff halfway" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Inverse, 11.0f, 1.0f, 11.0f ), 0.0, 1.0e-6, "inverse rolloff reaches 0 at the max distance" );
		C.Near( NAudio::RolloffGain( EAudioRolloff::Inverse, 1.0f, 1.0f, 11.0f ), 1.0, 1.0e-6, "inverse rolloff is 1 at the min distance" );
		C.Check( NAudio::RolloffGain( EAudioRolloff::Inverse, 2.0f, 1.0f, 11.0f ) < NAudio::RolloffGain( EAudioRolloff::Linear, 2.0f, 1.0f, 11.0f ), "inverse falls faster than linear near the source" );

		// パン
		FAudioListener Listener;   // 原点、前は +Z、上は +Y
		const FVector3 Right = NAudio::ListenerRight( Listener );
		C.Near( Right.x, 1.0, 1.0e-6, "listener right is +X" );
		C.Near( NAudio::SpatialPan( FVector3( 10, 0, 0 ), Right, 1.0f ), 1.0, 1.0e-6, "source on the right" );
		C.Near( NAudio::SpatialPan( FVector3( -10, 0, 0 ), Right, 1.0f ), -1.0, 1.0e-6, "source on the left" );
		C.Near( NAudio::SpatialPan( FVector3( 0, 0, 10 ), Right, 1.0f ), 0.0, 1.0e-6, "source in front" );
		C.Near( NAudio::SpatialPan( FVector3( 0, 0, -10 ), Right, 1.0f ), 0.0, 1.0e-6, "source behind" );
		C.Near( NAudio::SpatialPan( FVector3( 10, 0, 0 ), Right, 0.5f ), 0.5, 1.0e-6, "pan strength" );
		C.Near( NAudio::SpatialPan( FVector3::Zero, Right, 1.0f ), 0.0, 1.0e-6, "source at the listener" );
		Listener.Forward = FVector3::Right;   // 右を向く
		C.Near( NAudio::ListenerRight( Listener ).z, -1.0, 1.0e-6, "listener facing +X has its right at -Z" );

		C.Check( NAudio::VolumeToDecibel100( 1.0f ) == 0, "1.0 is 0 dB" );
		C.Check( NMath::Abs( NAudio::VolumeToDecibel100( 0.5f ) + 602 ) <= 1, "0.5 is about -6 dB" );
		C.Check( NAudio::VolumeToDecibel100( 0.0f ) == NAudio::MinDecibel100, "0 is the minimum" );
	}

	void TestFontFile( FChecker& C )
	{
		C.Group = "font-file";

		const std::vector<uint8> Both = MakeFakeFont( L"Test Face", L"Japanese" );
		FString Name;
		C.Check( NFontFile::ReadFaceNameFromMemory( Both.data(), Both.size(), Name ) && Name == "Test Face", "the English name is preferred (got '%s')", *Name );

		const std::vector<uint8> OnlyOther = MakeFakeFont( nullptr, L"Only" );
		C.Check( NFontFile::ReadFaceNameFromMemory( OnlyOther.data(), OnlyOther.size(), Name ) && Name == "Only", "another language is used when there is no English name" );

		const std::vector<uint8> Garbage( 64, 0xAB );
		C.Check( !NFontFile::ReadFaceNameFromMemory( Garbage.data(), Garbage.size(), Name ), "garbage is rejected" );
		C.Check( !NFontFile::ReadFaceNameFromMemory( Both.data(), 20, Name ), "a truncated file is rejected" );
		C.Check( !NFontFile::ReadFaceName( "this/file/does/not/exist.ttf", Name ), "a missing file is rejected" );

		// 本物のフォントがあれば（Windows の標準フォント）
		FString Arial;
		if ( NFontFile::ReadFaceName( "C:/Windows/Fonts/arial.ttf", Arial ) ) C.Check( Arial == "Arial", "arial.ttf is named 'Arial' (got '%s')", *Arial );
	}

	//=========================================================================
	// 2. スプライトシート・アニメーター（偽のバックエンドで）
	//=========================================================================
	struct FFixture
	{
		CAssetManager* Assets = nullptr;
		FFakeAssetBackend* Fake = nullptr;
		FString Dir;

		FString Path( const char* Name ) const { return Dir / Name; }
		void Touch( const char* Name ) const { WriteFileText( Path( Name ), "x" ); }
	};

	// 1 つの構造体を JSON にして、読み戻した結果（もう一度 JSON にしたもの）が同じか
	template<typename TDefinition>
	bool RoundTrips( const TDefinition& Definition )
	{
		const NlohmannJson Json = Definition.ToJson();
		TDefinition Back;
		if ( !TDefinition::FromJson( NlohmannJson::parse( Json.dump() ), Back ) ) return false;
		return Back.ToJson() == Json;
	}

	void TestSpriteSheet( FChecker& C, FFixture& F )
	{
		C.Group = "sprite-sheet";
		F.Touch( "sheet.png" );
		F.Fake->TextureWidth = 128;
		F.Fake->TextureHeight = 64;
		TSharedPtr<CTexture> Texture = F.Assets->LoadTexture( "sheet.png" );
		C.Check( Texture.IsValid() && Texture->GetWidth() == 128 && Texture->GetHeight() == 64, "the fake texture is 128x64" );
		if ( !Texture.IsValid() ) return;

		// 自動の大きさ: 4 x 2 -> 32 x 32
		FSpriteSheetGrid Grid;
		Grid.Columns = 4;
		Grid.Rows = 2;
		TSharedPtr<CSpriteSheet> Sheet = F.Assets->CreateSpriteSheet( Texture, Grid );
		C.Check( Sheet.IsValid() && Sheet->GetNumFrames() == 8, "4x2 grid has 8 frames" );
		if ( !Sheet.IsValid() ) return;
		const FSpriteFrame* Frame5 = Sheet->GetFrame( 5 );
		C.Check( Frame5 != nullptr && Frame5->X == 32 && Frame5->Y == 32 && Frame5->Width == 32 && Frame5->Height == 32, "frame 5 of an automatic 4x2 grid is (32,32 32x32)" );
		C.Check( Frame5 != nullptr && Frame5->PivotX == 16.0f && Frame5->PivotY == 16.0f, "the default pivot is the center" );
		const FSpriteFrame* Frame3 = Sheet->GetFrame( 3 );
		C.Check( Frame3 != nullptr && Frame3->X == 96 && Frame3->Y == 0, "frame 3 is at the end of the first row" );
		C.Check( Sheet->GetFrame( 8 ) == nullptr && Sheet->GetFrame( -1 ) == nullptr, "out-of-range frames are null" );

		// 列優先
		FSpriteSheetGrid Column = Grid;
		Column.Order = ESpriteGridOrder::ColumnMajor;
		TSharedPtr<CSpriteSheet> ColumnSheet = F.Assets->CreateSpriteSheet( Texture, Column );
		C.Check( ColumnSheet->GetFrame( 1 )->X == 0 && ColumnSheet->GetFrame( 1 )->Y == 32, "column-major: frame 1 is below frame 0" );
		C.Check( ColumnSheet->GetFrame( 2 )->X == 32 && ColumnSheet->GetFrame( 2 )->Y == 0, "column-major: frame 2 is the next column" );

		// 余白・隙間・明示した大きさ
		FSpriteSheetGrid Spaced;
		Spaced.Columns = 3;
		Spaced.Rows = 2;
		Spaced.CellWidth = 30;
		Spaced.CellHeight = 20;
		Spaced.MarginX = 4;
		Spaced.MarginY = 2;
		Spaced.SpacingX = 3;
		Spaced.SpacingY = 5;
		TSharedPtr<CSpriteSheet> SpacedSheet = F.Assets->CreateSpriteSheet( Texture, Spaced );
		C.Check( SpacedSheet->GetFrame( 4 )->X == 4 + 1 * ( 30 + 3 ) && SpacedSheet->GetFrame( 4 )->Y == 2 + 1 * ( 20 + 5 ), "explicit cells with margin and spacing" );

		// 自動 + 余白 + 隙間: (128 - 2*4 - 3*2) / 4 = 28
		FSpriteSheetGrid AutoSpaced;
		AutoSpaced.Columns = 4;
		AutoSpaced.Rows = 1;
		AutoSpaced.MarginX = 4;
		AutoSpaced.SpacingX = 2;
		TSharedPtr<CSpriteSheet> AutoSheet = F.Assets->CreateSpriteSheet( Texture, AutoSpaced );
		C.Check( AutoSheet->GetFrame( 0 )->Width == 28 && AutoSheet->GetFrame( 1 )->X == 4 + 28 + 2, "automatic cell size accounts for margin and spacing (28 px)" );

		// 使うコマの数・自由な矩形・基準点
		FSpriteSheetGrid Partial = Grid;
		Partial.NumFrames = 6;
		Partial.bCustomPivot = true;
		Partial.PivotX = 3.0f;
		Partial.PivotY = 30.0f;
		TSharedPtr<CSpriteSheet> PartialSheet = F.Assets->CreateSpriteSheet( Texture, Partial );
		C.Check( PartialSheet->GetNumFrames() == 6, "NumFrames limits the grid (6 of 8)" );
		C.Check( PartialSheet->GetFrame( 2 )->PivotX == 3.0f && PartialSheet->GetFrame( 2 )->PivotY == 30.0f, "custom pivot is applied to every cell" );
		const int32 Extra = PartialSheet->AddFrame( 100, 10, 20, 12 );
		C.Check( Extra == 6 && PartialSheet->GetFrame( Extra )->PivotX == 10.0f && PartialSheet->GetFrame( Extra )->PivotY == 6.0f, "free rectangles are appended after the grid" );

		// 画像からはみ出すコマは空になる
		FSpriteSheetGrid Outside;
		Outside.Columns = 5;
		Outside.Rows = 1;
		Outside.CellWidth = 30;
		Outside.CellHeight = 30;
		TSharedPtr<CSpriteSheet> OutsideSheet = F.Assets->CreateSpriteSheet( Texture, Outside );
		C.Check( OutsideSheet->GetFrame( 3 )->Width == 30 && OutsideSheet->GetFrame( 4 )->Width == 0, "a cell that does not fit in the texture is emptied" );

		// 非同期: 画像の大きさが分かるまで保留
		F.Touch( "late.png" );
		TSharedPtr<CTexture> Late = F.Assets->LoadTexture( "late.png", true );
		C.Check( Late.IsValid() && !Late->IsReady() && Late->GetWidth() == 0, "an async texture is not ready and has no size yet" );
		TSharedPtr<CSpriteSheet> LateSheet = F.Assets->CreateSpriteSheet( Late, Grid );
		C.Check( LateSheet->GetNumFrames() == 8 && LateSheet->GetFrame( 5 )->Width == 0 && !LateSheet->IsReady(), "frames of a pending texture are reserved but empty" );
		C.Check( F.Assets->GetNumLoading() >= 1, "the manager reports the async load" );
		F.Fake->FinishLoading();
		F.Assets->Tick( 0.0f );
		C.Check( LateSheet->IsReady() && LateSheet->GetFrame( 5 )->Width == 32 && LateSheet->GetFrame( 5 )->X == 32, "after loading finishes, the manager fills the grid" );

		// 定義と JSON
		Sheet->AddClip( FSpriteClip::FromRange( "Run", 0, 3, 14.0f ) );
		Sheet->AddClip( FSpriteClip::FromFrames( "Odd", { 0, 2, 1 }, 8.0f, EAnimLoopMode::PingPong ) );
		FSpriteClip Custom = FSpriteClip::FromRange( "Jump", 4, 7, 10.0f, EAnimLoopMode::Once );
		Custom.SetDuration( 1, 0.3f ).AddEvent( 2, "Land" ).AddEvent( 0, "Takeoff" );
		Sheet->AddClip( Custom );
		FSpriteSheetDefinition Definition = Sheet->GetDefinition();
		Definition.Frames.Add( FSpriteFrame { 1, 2, 3, 4, 1.5f, 2.0f } );
		Definition.Grids.Add( Spaced );
		C.Check( Definition.GetNumFrames() == 8 + 6 + 1, "definition frame count (grid + grid + free)" );
		C.Check( RoundTrips( Definition ), "a sprite sheet definition survives JSON" );

		const NlohmannJson Json = Definition.ToJson();
		const std::string Text = Json.dump();
		C.Check( Text.find( "14.0" ) != std::string::npos || Text.find( "14," ) != std::string::npos || Text.find( ":14" ) != std::string::npos, "fps is written plainly" );
		C.Check( Text.find( "0.30000" ) == std::string::npos && Text.find( "0.3" ) != std::string::npos, "float noise is rounded away (0.3, not 0.30000001...)" );
		C.Check( Json["Clips"][0].contains( "Range" ) && !Json["Clips"][1].contains( "Range" ) && Json["Clips"][1].contains( "Frames" ), "consecutive frames are saved as a Range" );
		C.Check( Json["Grid"].is_array() && Json["Grid"].size() == 2, "two grids are saved as an array" );

		FSpriteClip Clip = FSpriteClip::FromRange( "X", 0, 2, 10.0f );
		Clip.SetDuration( 1, 0.1f );   // 既定と同じ値
		Clip.NormalizeDurations();
		C.Check( Clip.Durations.IsEmpty(), "uniform durations are normalized away" );
		C.Near( FSpriteClip::FromRange( "T", 0, 3, 10.0f ).GetTotalDuration(), 0.4, 1.0e-6, "total duration" );
		C.Check( FSpriteClip::FromRange( "R", 5, 2 ).Frames == TArray<int32> { 5, 4, 3, 2 }, "reversed range" );

		// 定義ファイルの読み込み・保存（マネージャー）
		WriteFileText( F.Path( "hero.sheet.json" ), "{\"Texture\":\"sheet.png\",\"Grid\":{\"Columns\":4,\"Rows\":2},\"Clips\":[{\"Name\":\"Run\",\"Range\":[0,3],\"Fps\":10,\"Loop\":\"pingpong\",\"Events\":[{\"Step\":1,\"Name\":\"Step\"}]},{\"Name\":\"Bad\"}]}" );
		TSharedPtr<CSpriteSheet> Loaded = F.Assets->LoadSpriteSheet( "hero.sheet.json" );
		C.Check( Loaded.IsValid() && Loaded->GetNumFrames() == 8 && Loaded->GetNumClips() == 1, "the definition file loads (the clip without frames is skipped)" );
		if ( Loaded.IsValid() )
		{
			C.Check( Loaded->GetTexture() == Texture, "the sheet shares the already-loaded texture" );
			C.Check( Loaded->GetClip( 0 ) != nullptr && Loaded->GetClip( 0 )->Loop == EAnimLoopMode::PingPong && Loaded->GetClip( 0 )->Events.Num() == 1, "clip loop mode and events are read" );
			C.Check( Loaded->GetDefinition().TexturePath == "sheet.png", "the definition keeps the relative texture path" );
			C.Check( F.Assets->LoadSpriteSheet( "hero.sheet.json" ) == Loaded, "the same definition file is shared" );

			C.Check( F.Assets->SaveSpriteSheetDefinition( "saved/hero2.sheet.json", Loaded->GetDefinition() ), "the definition can be saved" );
			TSharedPtr<CSpriteSheet> Again = F.Assets->LoadSpriteSheet( "saved/hero2.sheet.json" );
			C.Check( Again.IsValid() && Again->GetDefinition().ToJson() == Loaded->GetDefinition().ToJson(), "a saved definition loads back identically" );
		}
		C.Check( F.Assets->LoadSpriteSheet( "missing.sheet.json" ) == nullptr, "a missing definition file returns null" );
	}

	void TestSpriteAnimator( FChecker& C, FFixture& F )
	{
		C.Group = "sprite-animator";
		F.Touch( "anim.png" );
		F.Fake->TextureWidth = 128;
		F.Fake->TextureHeight = 64;
		FSpriteSheetGrid Grid;
		Grid.Columns = 4;
		Grid.Rows = 2;
		TSharedPtr<CSpriteSheet> Sheet = F.Assets->CreateSpriteSheet( F.Assets->LoadTexture( "anim.png" ), Grid );
		Sheet->AddClip( FSpriteClip::FromRange( "run", 0, 3, 10.0f ).AddEvent( 2, "Step" ) );
		Sheet->AddClip( FSpriteClip::FromRange( "jump", 4, 7, 10.0f, EAnimLoopMode::Once ).AddEvent( 0, "Takeoff" ) );
		Sheet->AddClip( FSpriteClip::FromRange( "pp", 0, 2, 10.0f, EAnimLoopMode::PingPong ) );
		FSpriteClip Slow = FSpriteClip::FromRange( "slow", 0, 1, 10.0f );
		Slow.SetDuration( 0, 0.5f );
		Sheet->AddClip( Slow );

		FSpriteAnimator Animator( Sheet );
		C.Check( !Animator.Play( "nope" ) && Animator.GetFrameIndex() == INDEX_NONE, "an unknown clip is rejected" );
		C.Check( Animator.Play( "run" ) && Animator.IsPlaying() && Animator.GetClipName() == "run", "play" );

		// 0.05 -> 0.2 -> 0.3 -> 0.4 -> 0.5（境目を避けた時刻）
		Animator.Update( 0.05f );
		C.Check( Animator.GetFrameIndex() == 0 && Animator.GetStep() == 0, "t=0.05 shows frame 0" );
		C.Check( Animator.GetEvents().IsEmpty(), "no event yet" );
		Animator.Update( 0.10f );
		C.Check( Animator.GetFrameIndex() == 1, "t=0.15 shows frame 1" );
		Animator.Update( 0.10f );
		C.Check( Animator.GetFrameIndex() == 2 && Animator.HasEvent( "Step" ), "t=0.25 shows frame 2 and fires the event" );
		C.Check( Animator.ConsumeEvent( "Step" ) && !Animator.ConsumeEvent( "Step" ), "an event can be consumed once" );
		Animator.Update( 0.10f );
		C.Check( Animator.GetFrameIndex() == 3 && Animator.GetEvents().IsEmpty(), "t=0.35 shows frame 3; the event is gone" );
		Animator.Update( 0.10f );
		C.Check( Animator.GetFrameIndex() == 0, "t=0.45 wraps to frame 0" );
		Animator.Update( 0.30f );
		C.Check( Animator.HasEvent( "Step" ), "the event fires again in the next loop (t=0.75 crosses step 2)" );

		// 速さ・逆再生・一時停止
		Animator.Play( "run" );
		Animator.Speed = 2.0f;
		Animator.Update( 0.075f );   // 0.15 s of clip time
		C.Check( Animator.GetFrameIndex() == 1, "double speed" );
		Animator.Pause();
		Animator.Update( 1.0f );
		C.Check( Animator.GetFrameIndex() == 1 && !Animator.IsPlaying(), "paused animators do not move" );
		Animator.Resume();
		Animator.Speed = -1.0f;
		Animator.Play( "run" );   // 逆再生は最後のコマから
		Animator.Update( 0.0f );
		C.Check( Animator.GetFrameIndex() == 3, "reverse playback starts on the last frame (got %d)", Animator.GetFrameIndex() );
		Animator.Update( 0.15f );
		C.Check( Animator.GetFrameIndex() == 2, "reverse playback moves backwards (got %d)", Animator.GetFrameIndex() );
		Animator.Speed = 1.0f;

		// Once
		Animator.Play( "jump" );
		Animator.Update( 0.01f );
		C.Check( Animator.HasEvent( "Takeoff" ) && Animator.GetFrameIndex() == 4, "an event on the first frame fires as the clip starts" );
		Animator.Update( 0.34f );
		C.Check( Animator.GetFrameIndex() == 7 && !Animator.IsFinished() && Animator.IsPlaying(), "once: on the last frame, not finished yet" );
		Animator.Update( 0.10f );
		C.Check( Animator.GetFrameIndex() == 7 && Animator.IsFinished() && !Animator.IsPlaying() && Animator.DidFinishThisUpdate(), "once: stays on the last frame and reports finishing" );
		Animator.Update( 0.10f );
		C.Check( !Animator.DidFinishThisUpdate() && Animator.IsFinished(), "finishing is reported only once" );
		Animator.Resume();
		C.Check( !Animator.IsPlaying(), "a finished animator is not resumed" );
		Animator.Play( "jump" );
		C.Check( !Animator.IsFinished() && Animator.IsPlaying(), "playing again restarts" );

		// 往復: 0 1 2 1 0
		Animator.Play( "pp" );
		const int32 Expected[5] = { 0, 1, 2, 1, 0 };
		bool bPingPong = true;
		float32 Previous = 0.0f;
		for ( int32 i = 0; i < 5; ++i )
		{
			Animator.Update( i == 0 ? 0.05f : 0.10f );
			bPingPong = bPingPong && Animator.GetFrameIndex() == Expected[i];
			Previous = Animator.GetNormalizedTime();
		}
		C.Check( bPingPong, "ping-pong shows 0 1 2 1 0" );
		(void)Previous;

		// 表示時間をコマごとに変える（0.5 秒 + 0.1 秒）
		Animator.Play( "slow" );
		Animator.Update( 0.4f );
		C.Check( Animator.GetFrameIndex() == 0, "a long first frame (0.4 s in)" );
		Animator.Update( 0.2f );
		C.Check( Animator.GetFrameIndex() == 1, "then the second frame (0.6 s in)" );

		// 途中から・手動のコマ・止める
		Animator.PlayFrom( "run", 0.55f );
		Animator.Update( 0.0f );
		C.Check( Animator.GetFrameIndex() == 2, "PlayFrom 55%% starts on frame 2" );
		Animator.Stop();
		C.Check( !Animator.IsPlaying() && Animator.GetFrameIndex() == 0, "Stop rewinds to the first frame" );
		Animator.ShowFrame( 5 );
		C.Check( Animator.GetFrameIndex() == 5 && Animator.GetClipIndex() == INDEX_NONE, "ShowFrame shows a single frame" );

		// 描画コマンド
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Animator.Record( Draw, FVector2( 100.0f, 50.0f ), FVector2( 2.0f, 3.0f ), 0.5f );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		int32 NumSprites = 0;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::SpriteRect ) return;
			++NumSprites;
			const FDrawSpriteRectData Data = Command.Read<FDrawSpriteRectData>();
			C.Check( Data.X == 100.0f && Data.Y == 50.0f && Data.ScaleX == 2.0f && Data.ScaleY == 3.0f && Data.Angle == 0.5f, "sprite position, scale and angle" );
			C.Check( Data.SrcX == 32 && Data.SrcY == 32 && Data.SrcW == 32 && Data.SrcH == 32 && Data.PivotX == 16.0f, "sprite source rectangle (frame 5)" );
			C.Check( Data.Handle == Sheet->GetTextureHandle(), "sprite texture handle" );
		} );
		C.Check( NumSprites == 1, "one sprite is recorded" );

		Animator.bFlipX = true;
		Animator.Tint = FColor32( 255, 0, 0 );
		Animator.Alpha = 0.5f;
		Buffer.Clear();
		Animator.Record( Draw, FVector2( 0.0f, 0.0f ) );
		Log.Clear();
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		C.Check( Log.Count( EDrawCommand::Bright ) == 1 && Log.Count( EDrawCommand::Blend ) == 1 && Log.Count( EDrawCommand::SpriteRect ) == 1, "tint, alpha and sprite are recorded" );
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::SpriteRect ) C.Check( Command.Has( FDrawFlag::FlipX ) && !Command.Has( FDrawFlag::FlipY ), "flip flags" );
			if ( Command.Type == EDrawCommand::Blend ) C.Check( Command.Read<FDrawBlendData>().Param == 128, "alpha 0.5 is 128" );
			if ( Command.Type == EDrawCommand::Bright ) C.Check( Command.Read<FDrawBrightData>().Color.R == 255 && Command.Read<FDrawBrightData>().Color.G == 0, "tint color" );
		} );

		Animator.bVisible = false;
		Buffer.Clear();
		Animator.Record( Draw, FVector2( 0.0f, 0.0f ) );
		C.Check( Buffer.Words.empty(), "an invisible animator records nothing" );
		FSpriteAnimator Empty;
		Empty.Record( Draw, FVector2( 0.0f, 0.0f ) );
		Empty.Update( 1.0f );
		C.Check( Buffer.Words.empty() && !Empty.Play( "x" ), "an animator without a sheet does nothing" );
	}

	//=========================================================================
	// 3. モデルのアニメーター
	//=========================================================================
	const FDrawModelAnimData* FindLayer( const std::vector<FDrawModelAnimData>& Layers, int32 AttachIndex )
	{
		for ( const FDrawModelAnimData& Layer : Layers )
		{
			if ( Layer.AttachIndex == AttachIndex ) return &Layer;
		}
		return nullptr;
	}

	void CollectLayers( const FModelAnimator& Animator, std::vector<FDrawModelAnimData>& OutLayers, FDrawModelData* OutModel = nullptr, int32* OutNumModels = nullptr )
	{
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Animator.Record( Draw, FMatrix4x4::Identity );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		OutLayers.clear();
		int32 NumModels = 0;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::ModelAnim ) OutLayers.push_back( Command.Read<FDrawModelAnimData>() );
			if ( Command.Type == EDrawCommand::Model3D )
			{
				++NumModels;
				if ( OutModel != nullptr ) *OutModel = Command.Read<FDrawModelData>();
			}
		} );
		if ( OutNumModels != nullptr ) *OutNumModels = NumModels;
	}

	void TestModelAnimator( FChecker& C, FFixture& F )
	{
		C.Group = "model-animator";
		F.Touch( "robot.mv1" );
		F.Fake->NextModelData = { { "Idle", "Run" }, { 60.0f, 30.0f } };   // 2 秒と 1 秒（30 フレーム / 秒）
		TSharedPtr<CModel> Model = F.Assets->LoadModel( "robot.mv1" );
		C.Check( Model.IsValid() && Model->GetClips().Num() == 2 && Model->FindClip( "Run" ) != nullptr && Model->FindClip( "Run" )->AnimIndex == 1, "the model lists its animations" );
		if ( !Model.IsValid() ) return;

		TSharedPtr<CModelInstance> Instance = Model->CreateInstance();
		C.Check( Instance.IsValid() && Instance->GetHandle() != Model->GetHandle(), "an instance is a separate handle" );
		FModelAnimator Animator( Instance );
		C.Check( Animator.AddClipsFrom( Model ) == 2 && Animator.HasClip( "Idle" ) && Animator.HasClip( "Run" ), "AddClipsFrom registers every animation" );
		C.Near( Animator.GetClipLength( "Idle" ), 2.0, 1.0e-6, "Idle is 2 s (60 frames at 30 per second)" );
		C.Check( !Animator.Play( "Nope" ), "an unknown clip is rejected" );

		std::vector<FDrawModelAnimData> Layers;
		C.Check( Animator.Play( "Idle", 0.0f ) && Animator.IsPlaying() && Animator.GetCurrentClip() == "Idle", "play" );
		CollectLayers( Animator, Layers );
		C.Check( Layers.size() == 1 && Layers[0].Rate == 1.0f && Layers[0].Time == 0.0f, "one layer at full weight, time 0" );

		Animator.Update( 0.5f );
		CollectLayers( Animator, Layers );
		C.Near( Layers[0].Time, 15.0, 1.0e-3, "0.5 s is 15 model time units" );
		C.Near( Animator.GetNormalizedTime(), 0.25, 1.0e-4, "normalized time" );

		// クロスフェード: 0.5 秒かけて Run へ
		C.Check( Animator.Play( "Run", 0.5f ), "crossfade to Run" );
		Animator.Update( 0.25f );
		CollectLayers( Animator, Layers );
		const FDrawModelAnimData* Idle = FindLayer( Layers, 0 );
		const FDrawModelAnimData* Run = FindLayer( Layers, 1 );
		C.Check( Layers.size() == 2 && Idle != nullptr && Run != nullptr, "two layers while fading" );
		if ( Idle != nullptr && Run != nullptr )
		{
			C.Near( Idle->Rate, 0.5, 1.0e-4, "halfway: Idle weight 0.5" );
			C.Near( Run->Rate, 0.5, 1.0e-4, "halfway: Run weight 0.5" );
			C.Near( Idle->Time, 22.5, 1.0e-3, "Idle keeps advancing while it fades out" );
			C.Near( Run->Time, 7.5, 1.0e-3, "Run starts from 0" );
		}
		Animator.Update( 0.25f );
		CollectLayers( Animator, Layers );
		C.Near( FindLayer( Layers, 1 )->Rate, 1.0, 1.0e-4, "fade finished: Run weight 1" );
		C.Near( FindLayer( Layers, 0 )->Rate, 0.0, 1.0e-4, "fade finished: Idle weight 0" );
		C.Check( Animator.GetNumActiveLayers() == 1, "only Run is active after the fade" );
		const float32 FrozenIdleTime = FindLayer( Layers, 0 )->Time;
		Animator.Update( 0.8f );
		CollectLayers( Animator, Layers );
		C.Near( FindLayer( Layers, 1 )->Time, 9.0, 1.0e-2, "Run (1 s long) wraps: 1.3 s -> 9 units" );
		C.Near( FindLayer( Layers, 0 )->Time, FrozenIdleTime, 1.0e-4, "an inactive layer stays frozen" );

		// 出来事（0.5 の位置）
		C.Check( Animator.AddEvent( "Run", 0.5f, "Footstep" ) && !Animator.AddEvent( "Nope", 0.5f, "X" ), "events are added to existing clips only" );
		FModelPlayParams Restart;
		Restart.FadeTime = 0.0f;
		Restart.bRestartIfPlaying = true;
		Animator.Play( "Run", Restart );
		Animator.Update( 0.6f );
		C.Check( Animator.HasEvent( "Footstep" ), "the event fires when the playhead crosses 0.5" );
		Animator.Update( 0.3f );
		C.Check( Animator.GetEvents().IsEmpty(), "and not again" );
		Animator.Update( 0.7f );   // 0.9 -> 1.6 : 1.5 を通る
		C.Check( Animator.ConsumeEvent( "Footstep" ), "it fires again in the next loop" );

		// Once
		FModelPlayParams Once;
		Once.FadeTime = 0.0f;
		Once.Loop = EAnimLoopMode::Once;
		Once.bRestartIfPlaying = true;
		Animator.Play( "Idle", Once );
		Animator.Update( 3.0f );
		CollectLayers( Animator, Layers );
		C.Check( Animator.IsFinished() && Animator.DidFinishThisUpdate() && !Animator.IsPlaying(), "once: finished" );
		C.Near( FindLayer( Layers, 0 )->Time, 60.0, 1.0e-3, "once: holds the last pose" );
		Animator.Update( 0.1f );
		C.Check( !Animator.DidFinishThisUpdate() && Animator.IsFinished(), "once: the finish is reported once" );
		Animator.Play( "Idle", Once );
		C.Check( !Animator.IsFinished() && Animator.IsPlaying(), "once: playing again restarts" );

		// 逆再生
		FModelPlayParams Reverse;
		Reverse.FadeTime = 0.0f;
		Reverse.Speed = -1.0f;
		Reverse.bRestartIfPlaying = true;
		Animator.Play( "Run", Reverse );
		Animator.Update( 0.25f );
		CollectLayers( Animator, Layers );
		C.Near( FindLayer( Layers, 1 )->Time, 22.5, 0.5, "reverse playback starts at the end and moves back (22.5 units after 0.25 s)" );

		// 描画コマンド
		FDrawModelData ModelCommand;
		int32 NumModels = 0;
		Animator.ModelOffset = FVector3( 1.0f, 2.0f, 3.0f );
		CollectLayers( Animator, Layers, &ModelCommand, &NumModels );
		C.Check( NumModels == 1 && ModelCommand.Handle == Instance->GetHandle(), "one Model3D command with the instance handle" );
		C.Check( ModelCommand.M[9] == 1.0f && ModelCommand.M[10] == 2.0f && ModelCommand.M[11] == 3.0f, "the model offset is in the matrix translation" );

		// 止める: すべての層のブレンド率が 0（前の値を残さない）
		Animator.Stop( 0.0f );
		CollectLayers( Animator, Layers );
		bool bAllZero = !Layers.empty();
		for ( const FDrawModelAnimData& Layer : Layers ) bAllZero = bAllZero && Layer.Rate == 0.0f;
		C.Check( bAllZero && !Animator.IsPlaying() && Animator.GetCurrentClip().IsEmpty(), "Stop zeroes every attached layer" );

		// クリップごとの既定の速さ・フェード（Run と同じアニメーションを付けると番号が重なるので、別のアニメーターで）
		FModelAnimator Defaults( Model->CreateInstance() );
		Defaults.AddClip( "Base", Model, 0 );
		Defaults.AddClip( "Fast", Model, 1, EAnimLoopMode::Loop, 2.0f, 0.5f );
		Defaults.Play( "Base", 0.0f );
		Defaults.Play( "Fast" );   // 既定のフェード 0.5 秒
		Defaults.Update( 0.25f );
		CollectLayers( Defaults, Layers );
		C.Near( FindLayer( Layers, 1 )->Rate, 0.5, 1.0e-3, "the clip's default fade (0.5 s) is used" );
		C.Near( FindLayer( Layers, 1 )->Time, 15.0, 1.0e-2, "the clip's default speed (x2) is used: 0.25 s -> 0.5 s of clip -> 15 units" );

		// 付けるアニメーションの数の上限（8 個のクリップを順に再生しても 6 つまで）
		F.Touch( "many.mv1" );
		F.Fake->NextModelData = {};
		for ( int32 i = 0; i < 8; ++i )
		{
			F.Fake->NextModelData.Names.push_back( FString::Printf( "A%d", i ) );
			F.Fake->NextModelData.Lengths.push_back( 30.0f );
		}
		TSharedPtr<CModel> Many = F.Assets->LoadModel( "many.mv1" );
		TSharedPtr<CModelInstance> ManyInstance = Many->CreateInstance();
		FModelAnimator ManyAnimator( ManyInstance );
		ManyAnimator.AddClipsFrom( Many );
		const int32 DetachesBefore = F.Fake->NumDetachCalls;
		for ( int32 i = 0; i < 8; ++i ) ManyAnimator.Play( FString::Printf( "A%d", i ), 0.0f );
		C.Check( static_cast<int32>( F.Fake->Attached[ManyInstance->GetHandle()].size() ) <= FModelAnimator::MaxLayers, "at most %d animations are attached at once", FModelAnimator::MaxLayers );
		C.Check( F.Fake->NumDetachCalls - DetachesBefore >= 2, "older animations are detached to make room" );
		const int32 AttachesBefore = F.Fake->NumAttachCalls;
		ManyAnimator.Play( "A7", 0.0f );
		C.Check( F.Fake->NumAttachCalls == AttachesBefore, "playing a clip that is still attached does not attach it again" );
	}

	void TestModelAnimationAsset( FChecker& C, FFixture& F )
	{
		C.Group = "model-animation-asset";
		F.Touch( "hero.mv1" );
		F.Touch( "walk.mv1" );

		// 定義の読み書き
		FModelAnimationDefinition Definition;
		Definition.ModelPath = "hero.mv1";
		Definition.UnitsPerSecond = 24.0f;
		Definition.Offset = FVector3( 1.0f, 2.0f, 3.0f );
		Definition.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Definition.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		FModelClipDef Idle;
		Idle.Name = "Idle";
		Idle.AnimIndex = 0;
		FModelClipDef Walk;
		Walk.Name = "Walk";
		Walk.SourcePath = "walk.mv1";
		Walk.AnimName = "mixamo.com";
		Walk.AnimIndex = 5;   // 名前が優先されるので使われない
		Walk.Loop = EAnimLoopMode::PingPong;
		Walk.Speed = 1.5f;
		Walk.FadeTime = 0.3f;
		Walk.Events.Add( FModelEventDef { 0.25f, "Footstep" } );
		Walk.Events.Add( FModelEventDef { 0.75f, "Footstep" } );
		Definition.Clips.Add( Idle );
		Definition.Clips.Add( Walk );
		C.Check( RoundTrips( Definition ), "a model animation definition survives JSON" );
		C.Check( Definition.FindClip( "Walk" ) == 1 && Definition.FindClip( "Nope" ) == INDEX_NONE, "FindClip" );

		C.Check( F.Assets->SaveModelAnimationDefinition( "hero.modelanim.json", Definition ), "the definition can be saved" );

		// 読み込み（モデルを先に読み込んで、それぞれ違うアニメーションを持たせる）
		F.Fake->NextModelData = { { "mixamo.com" }, { 45.0f } };
		TSharedPtr<CModel> WalkModel = F.Assets->LoadModel( "walk.mv1" );
		F.Fake->NextModelData = { { "Idle", "Run" }, { 60.0f, 30.0f } };
		TSharedPtr<CModelAnimationAsset> Asset = F.Assets->LoadModelAnimations( "hero.modelanim.json" );
		C.Check( Asset.IsValid() && Asset->IsReady() && Asset->GetDefinition().Clips.Num() == 2, "the definition file loads" );
		if ( !Asset.IsValid() ) return;
		C.Check( Asset->GetClipSource( 1 ) == WalkModel && Asset->GetClipSource( 0 ) == Asset->GetModel(), "clip sources are resolved (own model / another file)" );
		C.Check( F.Assets->LoadModelAnimations( "hero.modelanim.json" ) == Asset, "the same definition is shared" );
		C.Check( F.Assets->LoadModelAnimations( "missing.modelanim.json" ) == nullptr, "a missing definition returns null" );

		FModelAnimator Animator = Asset->CreateAnimator();
		C.Check( Animator.Instance.IsValid() && Animator.HasClip( "Idle" ) && Animator.HasClip( "Walk" ), "CreateAnimator registers the clips" );
		C.Near( Animator.UnitsPerSecond, 24.0, 1.0e-6, "units per second comes from the definition" );
		C.Near( Animator.GetClipLength( "Walk" ), 45.0 / 24.0, 1.0e-4, "the clip is found by its animation name in the other file (45 units at 24 per second)" );

		C.Check( Animator.Play( "Idle", 0.0f ), "play Idle" );
		Animator.Play( "Walk" );   // 定義の既定: フェード 0.3 秒、速さ 1.5
		Animator.Update( 0.15f );
		std::vector<FDrawModelAnimData> Layers;
		FDrawModelData ModelCommand;
		CollectLayers( Animator, Layers, &ModelCommand );
		C.Check( Layers.size() == 2, "Idle fades out while Walk fades in" );
		if ( Layers.size() == 2 )
		{
			float32 WalkRate = 0.0f;
			float32 WalkTime = 0.0f;
			for ( const FDrawModelAnimData& Layer : Layers )
			{
				if ( Layer.Time > 0.0f && Layer.Rate > 0.0f && Layer.AttachIndex != 0 ) { WalkRate = Layer.Rate; WalkTime = Layer.Time; }
			}
			C.Near( WalkRate, 0.5, 1.0e-3, "the default fade of 0.3 s: halfway after 0.15 s" );
			C.Near( WalkTime, 0.15 * 1.5 * 24.0, 1.0e-2, "the default speed of 1.5 is applied" );
		}
		// 見た目の補正: 拡大 2 倍、移動は Offset
		C.Near( std::sqrt( ModelCommand.M[0] * ModelCommand.M[0] + ModelCommand.M[1] * ModelCommand.M[1] + ModelCommand.M[2] * ModelCommand.M[2] ), 2.0, 1.0e-4, "the model scale comes from the definition" );
		C.Check( ModelCommand.M[9] == 1.0f && ModelCommand.M[10] == 2.0f && ModelCommand.M[11] == 3.0f, "the model offset comes from the definition" );

		// 出来事（0.25 と 0.75 の 2 回）
		FModelPlayParams Params;
		Params.FadeTime = 0.0f;
		Params.bRestartIfPlaying = true;
		Animator.Play( "Walk", Params );
		int32 NumFootsteps = 0;
		for ( int32 i = 0; i < 40; ++i )
		{
			Animator.Update( 0.05f );
			while ( Animator.ConsumeEvent( "Footstep" ) ) ++NumFootsteps;
		}
		// Walk は PingPong（1.875 s の往復 = 周期 3.75 s）。2.0 s の間に 0.25 / 0.75 を往路で、復路（3.75 - 0.75 = 3.0 s ...）はまだ。
		// 速さ 1.5 なので 2.0 s は 3.0 s 分: 往路の 0.25 と 0.75 (0.469 s, 1.406 s) と、復路の 0.75 (2.344 s) と 0.25 (3.281 s は超える)
		C.Check( NumFootsteps == 3, "the events of the definition fire three times in 3 s of clip time (got %d)", NumFootsteps );

		// 編集して作り直す（エディタのプレビュー）
		FModelAnimationDefinition Edited = Asset->GetDefinition();
		Edited.Clips[0].Name = "Rest";
		TSharedPtr<CModelAnimationAsset> EditedAsset = F.Assets->CreateModelAnimations( Edited );
		C.Check( EditedAsset.IsValid() && EditedAsset != Asset && EditedAsset->CreateAnimator().HasClip( "Rest" ), "CreateModelAnimations builds a fresh asset from a definition" );

		C.Check( F.Assets->SaveModelAnimationDefinition( "saved/hero2.modelanim.json", Asset->GetDefinition() ), "save into a sub-folder" );
		TSharedPtr<CModelAnimationAsset> Again = F.Assets->LoadModelAnimations( "saved/hero2.modelanim.json" );
		C.Check( Again.IsValid() && Again->GetDefinition().ToJson() == Asset->GetDefinition().ToJson(), "a saved definition loads back identically" );
	}

	//=========================================================================
	// 4. アセットの管理
	//=========================================================================
	void TestAssetManager( FChecker& C, FFixture& F )
	{
		C.Group = "asset-manager";
		CAssetManager& Assets = *F.Assets;
		FFakeAssetBackend& Fake = *F.Fake;
		F.Touch( "tex.png" );
		F.Touch( "other.png" );
		F.Touch( "sound.wav" );
		F.Touch( "model.mv1" );

		// 共有
		const int32 LoadsBefore = Fake.NumTextureLoads;
		TSharedPtr<CTexture> A = Assets.LoadTexture( "tex.png" );
		TSharedPtr<CTexture> B = Assets.LoadTexture( "tex.png" );
		C.Check( A.IsValid() && A == B && Fake.NumTextureLoads == LoadsBefore + 1, "the same file is loaded once and shared" );
		C.Check( Assets.LoadTexture( "TEX.PNG" ) == A, "the path is case-insensitive" );
		C.Check( Assets.LoadTexture( "./tex.png" ) == A, "a leading ./ makes no difference" );
		F.Touch( "sub.png" );
		TSharedPtr<CTexture> InSub = Assets.LoadTexture( "sub.png" );
		C.Check( Assets.LoadTexture( ".\\sub.png" ) == InSub, "back-slashes make no difference" );
		TSharedPtr<CTexture> Other = Assets.LoadTexture( "other.png" );
		C.Check( Other.IsValid() && Other != A, "another file is another asset" );

		// 失敗
		const int32 HandlesBefore = static_cast<int32>( Fake.Textures.size() );
		C.Check( Assets.LoadTexture( "no/such/file.png" ) == nullptr && static_cast<int32>( Fake.Textures.size() ) == HandlesBefore, "a missing file returns null without touching the backend" );

		// 解放: 最後の参照が消えたとき
		const int32 AliveBefore = static_cast<int32>( Fake.Textures.size() );
		const int32 Handle = Other->GetHandle();
		Other.Reset();
		C.Check( static_cast<int32>( Fake.Textures.size() ) == AliveBefore - 1 && Fake.Textures.count( Handle ) == 0, "the resource is released when the last reference goes" );
		Assets.ReleaseUnused();
		TSharedPtr<CTexture> Reloaded = Assets.LoadTexture( "other.png" );
		C.Check( Reloaded.IsValid() && Fake.Textures.count( Reloaded->GetHandle() ) == 1, "an unloaded asset can be loaded again" );

		// 別のスレッドで最後の参照を手放す: 解放はメインスレッドで（次の更新で）
		TSharedPtr<CTexture> Foreign = Assets.LoadTexture( "tex.png" );
		F.Touch( "foreign.png" );
		TSharedPtr<CTexture> ForeignOnly = Assets.LoadTexture( "foreign.png" );
		const int32 ForeignHandle = ForeignOnly->GetHandle();
		std::thread Worker( [Moved = MoveTemp( ForeignOnly )]() mutable { Moved.Reset(); } );
		Worker.join();
		C.Check( Fake.Textures.count( ForeignHandle ) == 1, "a release on another thread is deferred (the backend is not touched)" );
		Assets.Tick( 0.0f );
		C.Check( Fake.Textures.count( ForeignHandle ) == 0, "the deferred release happens on the next update" );

		// バンドル
		F.Touch( "bundled.png" );
		FAssetBundle Bundle;
		int32 BundledHandle = -1;
		{
			TSharedPtr<CTexture> Bundled = Assets.LoadTexture( "bundled.png" );
			BundledHandle = Bundled->GetHandle();
			Bundle.Add( Bundled );
		}
		C.Check( Bundle.Num() == 1 && Fake.Textures.count( BundledHandle ) == 1, "a bundle keeps assets alive" );
		Bundle.Clear();
		C.Check( Fake.Textures.count( BundledHandle ) == 0, "clearing the bundle releases them" );

		// サウンドの読み込み方は別のアセット
		TSharedPtr<CSound> Memory = Assets.LoadSound( "sound.wav" );
		TSharedPtr<CSound> Stream = Assets.LoadSound( "sound.wav", ESoundLoadMode::Stream );
		C.Check( Memory.IsValid() && Stream.IsValid() && Memory != Stream && Assets.LoadSound( "sound.wav" ) == Memory, "memory and stream versions of a file are separate assets" );
		const int32 Voice1 = Memory->AcquireVoice();
		const int32 Voice2 = Memory->AcquireVoice();
		C.Check( Voice1 >= 0 && Voice2 >= 0 && Voice1 != Voice2 && Memory->GetNumVoicesInUse() == 2, "a memory sound lends several voices" );
		Memory->ReleaseVoice( Voice1 );
		C.Check( Memory->GetNumIdleVoices() == 1 && Memory->AcquireVoice() == Voice1, "a returned voice is reused" );
		Memory->ReleaseVoice( Voice1 );
		Memory->ReleaseVoice( Voice2 );
		const int32 Duplicates = Fake.NumDuplicates;
		const int32 StreamVoice = Stream->AcquireVoice();
		C.Check( StreamVoice == Stream->GetHandle() && Stream->AcquireVoice() < 0 && Fake.NumDuplicates == Duplicates, "a stream sound lends its own handle, once" );
		Stream->ReleaseVoice( StreamVoice );
		C.Check( Stream->AcquireVoice() == StreamVoice, "and again after it is returned" );

		// フォント: ファイルは数えながら登録し、最後の利用者がいなくなったら外す
		WriteFileBytes( F.Path( "face.ttf" ), MakeFakeFont( L"Test Face", nullptr ) );
		{
			TSharedPtr<CFont> Small = Assets.LoadFont( "face.ttf", 16 );
			TSharedPtr<CFont> Large = Assets.LoadFont( "face.ttf", 32 );
			TSharedPtr<CFont> SmallAgain = Assets.LoadFont( "face.ttf", 16 );
			C.Check( Small.IsValid() && Large.IsValid() && Small != Large && Small == SmallAgain, "fonts of different sizes are separate; the same size is shared" );
			C.Check( Small.IsValid() && Small->GetDesc().Face == "Test Face" && Small->GetSize() == 16, "the face name is read from the font file" );
			C.Check( Fake.FontFilesAdded.size() == 1, "the font file is registered once for both sizes" );
			C.Check( Small.IsValid() && Small->MeasureWidth( "abc" ) == 30, "text width goes through the backend" );

			FFontStyle Edge;
			Edge.EdgeSize = 2;
			TSharedPtr<CFont> Edged = Assets.LoadFont( "face.ttf", 16, Edge );
			C.Check( Edged.IsValid() && Edged != Small && Edged->GetDesc().EdgeSize == 2, "a different style is a different font" );
			TSharedPtr<CFont> Default = Assets.GetDefaultFont( 20 );
			TSharedPtr<CFont> System = Assets.LoadSystemFont( "Meiryo", 20 );
			C.Check( Default.IsValid() && Default->GetDesc().Face.IsEmpty() && System.IsValid() && System->GetDesc().Face == "Meiryo" && Default != System, "default and named system fonts" );
			C.Check( Assets.LoadSystemFont( "meiryo", 20 ) == System, "system font names are case-insensitive" );
		}
		C.Check( Fake.FontFilesRemoved.size() == 1, "the font file is unregistered when its last font goes (removed %d time(s))", static_cast<int32>( Fake.FontFilesRemoved.size() ) );

		// 集計
		Fake.NextModelData = { { "A" }, { 30.0f } };
		TSharedPtr<CModel> Model = Assets.LoadModel( "model.mv1" );
		Assets.UpdateStats();
		const FAssetStats& Stats = Assets.GetStats();
		C.Check( Stats.Get( EAssetType::Model ).Count == 1 && Stats.Get( EAssetType::Model ).MemoryBytes == 10000, "model stats (count and the backend's estimate)" );
		C.Check( Stats.Get( EAssetType::Sound ).Count == 2 && Stats.Get( EAssetType::Sound ).MemoryBytes == 40000 + 1000, "sound stats (memory 40000 + stream 1000)" );
		const int64 TextureBytes = Stats.Get( EAssetType::Texture ).MemoryBytes;
		C.Check( TextureBytes == 128 * 64 * 4 * Stats.Get( EAssetType::Texture ).Count, "texture memory is width x height x 4 per texture (%lld)", static_cast<long long>( TextureBytes ) );
		C.Check( Stats.TotalMemoryBytes >= TextureBytes + 41000 + 10000 && Stats.PeakMemoryBytes >= Stats.TotalMemoryBytes, "totals and the peak" );
		C.Check( Stats.NumCacheHits >= 2 && Stats.NumLoads >= 5 && Stats.NumLoadFailures >= 1, "load, cache-hit and failure counts" );
		C.Check( Stats.Backend.VideoMemoryTotal == 1000000 && Stats.Backend.VideoMemoryUsed == 250000, "the backend's video memory is passed through" );
		C.Check( Stats.NumDeferredReleases >= 1, "deferred releases are counted" );
		bool bHasRefCounts = false;
		for ( const FAssetInfo& Info : Assets.GetLoadedAssets() )
		{
			if ( Info.Type == EAssetType::Texture && Info.RefCount >= 2 ) bHasRefCounts = true;
		}
		C.Check( bHasRefCounts, "the asset list reports reference counts" );
		Assets.DumpToLog();

		// 非同期のサウンドは、読み込みが終わるまで鳴らせない（CAudioSystem の確認で使う）
	}

	//=========================================================================
	// 5. オーディオ
	//=========================================================================
	int32 FindStartedVoice( const FRecordingAudioBackend& Rec ) { return Rec.LastStarted; }

	void TestAudioSystem( FChecker& C, FFixture& F )
	{
		C.Group = "audio-system";
		CAssetManager& Assets = *F.Assets;
		F.Touch( "a.wav" );
		F.Touch( "b.wav" );
		F.Touch( "m1.ogg" );
		F.Touch( "m2.ogg" );
		F.Touch( "c.wav" );
		F.Fake->SoundLength = 2.0f;
		TSharedPtr<CSound> A = Assets.LoadSound( "a.wav" );
		TSharedPtr<CSound> B = Assets.LoadSound( "b.wav" );
		TSharedPtr<CSound> Chirp = Assets.LoadSound( "c.wav" );
		TSharedPtr<CSound> M1 = Assets.LoadSound( "m1.ogg", ESoundLoadMode::Stream );
		TSharedPtr<CSound> M2 = Assets.LoadSound( "m2.ogg", ESoundLoadMode::Stream );

		FRecordingAudioBackend Rec;
		CAudioSystem Audio;
		Audio.SetBackendOverride( &Rec );
		Audio.SetRandomSeed( 1234 );

		// 基本: 音量・パン・ピッチ・ループがバックエンドへ届く
		FSoundPlayParams P;
		P.Volume = 0.5f;
		P.Pan = -0.5f;
		P.Pitch = 2.0f;
		P.bLoop = true;
		FSoundHandle H = Audio.Play( A, P );
		C.Check( H.IsValid() && Audio.IsPlaying( H ) && Audio.GetNumVoices() == 1, "play" );
		FRecordingAudioBackend::FVoiceState* V = &Rec.Voices[Rec.LastStarted];
		C.Near( V->Volume, 0.5, 1.0e-6, "volume reaches the backend" );
		C.Near( V->Pan, -0.5, 1.0e-6, "pan reaches the backend" );
		C.Near( V->Pitch, 2.0, 1.0e-6, "pitch reaches the backend" );
		C.Check( V->bPlaying && V->bLoop && A->GetNumVoicesInUse() == 1, "the voice is playing and looping, and borrowed from the sound" );

		// 即座に止める: ボイスは返される
		Audio.Stop( H );
		C.Check( !Audio.IsPlaying( H ) && Audio.GetNumVoices() == 0 && !Rec.Voices[Rec.LastStarted].bPlaying && A->GetNumIdleVoices() == 1, "stop frees the voice and returns it to the pool" );

		// フェードイン
		FSoundPlayParams FadeIn;
		FadeIn.FadeInTime = 1.0f;
		H = Audio.Play( A, FadeIn );
		V = &Rec.Voices[Rec.LastStarted];
		C.Near( V->Volume, 0.0, 1.0e-6, "a fade-in starts silent" );
		Audio.Update( 0.5f );
		C.Near( V->Volume, 0.5, 1.0e-4, "fade-in halfway" );
		Audio.Update( 0.5f );
		C.Near( V->Volume, 1.0, 1.0e-4, "fade-in done" );

		// バスとマスター
		Audio.SetBusVolume( EAudioBus::Sfx, 0.5f );
		Audio.Update( 0.0f );
		C.Near( V->Volume, 0.5, 1.0e-4, "bus volume scales the voice" );
		Audio.SetMasterVolume( 0.0f, 2.0f );
		Audio.Update( 1.0f );
		C.Near( V->Volume, 0.25, 1.0e-4, "master fade halfway (0.5 x 0.5)" );
		Audio.Update( 1.0f );
		C.Near( V->Volume, 0.0, 1.0e-4, "master fade done" );
		Audio.SetMasterVolume( 1.0f );
		Audio.SetBusMuted( EAudioBus::Sfx, true );
		Audio.Update( 0.0f );
		C.Near( V->Volume, 0.0, 1.0e-6, "a muted bus is silent" );
		Audio.SetBusMuted( EAudioBus::Sfx, false );
		Audio.SetBusVolume( EAudioBus::Sfx, 1.0f );
		Audio.Update( 0.0f );
		C.Near( V->Volume, 1.0, 1.0e-4, "unmuting restores the volume" );

		// 音量・ピッチ・パンのフェード
		Audio.SetVolume( H, 0.0f, 1.0f );
		Audio.SetPitch( H, 2.0f, 1.0f );
		Audio.SetPan( H, 1.0f, 1.0f );
		Audio.Update( 0.5f );
		C.Near( V->Volume, 0.5, 1.0e-4, "volume fade" );
		C.Near( V->Pitch, 1.5, 1.0e-4, "pitch fade" );
		C.Near( V->Pan, 0.5, 1.0e-4, "pan fade" );
		Audio.Update( 0.5f );
		Audio.SetVolume( H, 1.0f );
		Audio.SetPitch( H, 1.0f );
		Audio.SetPan( H, 0.0f );
		Audio.Update( 0.0f );

		// フェードアウトして止める
		Audio.Stop( H, 1.0f );
		C.Check( !Audio.IsPlaying( H ), "a stopping voice no longer counts as playing" );
		Audio.Update( 0.5f );
		C.Near( V->Volume, 0.5, 1.0e-4, "fade-out halfway" );
		C.Check( V->bPlaying, "still sounding while it fades" );
		Audio.Update( 0.6f );
		C.Check( !V->bPlaying && Audio.GetNumVoices() == 0, "the voice stops when the fade ends" );

		// 最後まで鳴った音は片付く
		H = Audio.Play( A );
		const int32 Finishing = Rec.LastStarted;
		Rec.Finish( Finishing );
		Audio.Update( 0.0f );
		C.Check( !Audio.IsPlaying( H ) && Audio.GetNumVoices() == 0, "a sound that finished by itself is freed" );

		// 古いハンドルは、同じ場所が別の音に使われても無効のまま
		FSoundHandle Old = Audio.Play( A );
		Audio.Stop( Old );
		FSoundHandle Newer = Audio.Play( B );
		const int32 NewerVoice = Rec.LastStarted;
		C.Check( Old != Newer && !Audio.IsPlaying( Old ) && Audio.IsPlaying( Newer ), "stale handles stay invalid after the slot is reused" );
		Audio.SetVolume( Old, 0.0f );
		Audio.Update( 0.0f );
		C.Near( Rec.Voices[NewerVoice].Volume, 1.0, 1.0e-6, "a stale handle does not affect the new voice" );
		Audio.StopAll();
		C.Check( Audio.GetNumVoices() == 0, "StopAll" );

		// 3D の位置（リスナーは原点、前は +Z）
		FSoundPlayParams Spatial;
		Spatial.bSpatial = true;
		Spatial.Position = FVector3( 10.0f, 0.0f, 0.0f );
		Spatial.MinDistance = 1.0f;
		Spatial.MaxDistance = 21.0f;
		H = Audio.Play( A, Spatial );
		V = &Rec.Voices[Rec.LastStarted];
		C.Near( V->Volume, 0.55, 1.0e-4, "spatial: 10 m away with a 1..21 range is 0.55" );
		C.Near( V->Pan, 1.0, 1.0e-4, "spatial: a source on the right pans right" );
		Audio.SetPosition( H, FVector3( -10.0f, 0.0f, 0.0f ) );
		Audio.Update( 0.0f );
		C.Near( V->Pan, -1.0, 1.0e-4, "spatial: moving the source to the left pans left" );
		Audio.SetPosition( H, FVector3( 0.0f, 0.0f, 10.0f ) );
		Audio.Update( 0.0f );
		C.Near( V->Pan, 0.0, 1.0e-4, "spatial: in front is centered" );
		Audio.SetListener( FVector3( 10.0f, 0.0f, 0.0f ) );
		Audio.SetPosition( H, FVector3( 10.0f, 0.0f, 5.0f ) );
		Audio.Update( 0.0f );
		C.Near( V->Volume, 0.8, 1.0e-4, "spatial: the listener moved, 5 m away is 0.8" );
		Audio.SetListener( FVector3::Zero );
		Audio.Stop( H );
		FSoundHandle Far = Audio.PlayAt( A, FVector3( 0.0f, 0.0f, 500.0f ) );
		C.Near( Rec.Voices[Rec.LastStarted].Volume, 0.0, 1.0e-6, "PlayAt: beyond the max distance is silent (but still occupies a voice)" );
		Audio.Stop( Far );

		// ばらつきは種が同じなら同じ並び
		{
			FSoundPlayParams Varied;
			Varied.VolumeVariation = 0.2f;
			std::vector<float32> First, Second;
			double Sum = 0.0;
			bool bInRange = true;
			for ( int32 Pass = 0; Pass < 2; ++Pass )
			{
				CAudioSystem Other;
				FRecordingAudioBackend OtherRec;
				Other.SetBackendOverride( &OtherRec );
				Other.SetRandomSeed( 1234 );
				for ( int32 i = 0; i < 200; ++i )
				{
					FSoundHandle Handle = Other.Play( A, Varied );
					const float32 Volume = OtherRec.Voices[OtherRec.LastStarted].Volume;
					( Pass == 0 ? First : Second ).push_back( Volume );
					if ( Pass == 0 )
					{
						Sum += Volume;
						bInRange = bInRange && Volume >= 0.8f - 1.0e-5f && Volume <= 1.2f + 1.0e-5f;
					}
					Other.Stop( Handle );
				}
				Other.SetBackendOverride( nullptr );
			}
			C.Check( bInRange, "volume variation stays within +-20%%" );
			C.Check( First == Second, "the same seed gives the same variation" );
			C.Near( Sum / 200.0, 1.0, 0.05, "the variation is centered on the requested volume" );
			C.Check( std::set<float32>( First.begin(), First.end() ).size() > 100, "the variation really varies" );
		}

		// 同時に鳴らせる数
		{
			CAudioSystem Limited;
			FRecordingAudioBackend LimitedRec;
			Limited.SetBackendOverride( &LimitedRec );
			Limited.SetMaxVoices( 3 );
			FSoundHandle Handles[3];
			for ( FSoundHandle& Handle : Handles ) Handle = Limited.Play( A );
			C.Check( Limited.GetNumVoices() == 3, "three voices" );
			FSoundPlayParams Low;
			Low.Priority = -1;
			C.Check( !Limited.Play( A, Low ).IsValid() && Limited.GetNumVoices() == 3, "a lower priority sound is rejected when the voices are full" );
			FSoundPlayParams High;
			High.Priority = 5;
			FSoundHandle Stealer = Limited.Play( A, High );
			C.Check( Stealer.IsValid() && Limited.GetNumVoices() == 3, "a higher priority sound takes a voice" );
			int32 NumStillValid = 0;
			for ( const FSoundHandle& Handle : Handles ) NumStillValid += Limited.IsPlaying( Handle ) ? 1 : 0;
			C.Check( NumStillValid == 2, "exactly one old voice was stopped" );
			Limited.StopAll();

			// 優先度が混ざっているときは、いちばん低いものが止められる
			Limited.SetMaxVoices( 3 );
			FSoundPlayParams P5, P0, P3;
			P5.Priority = 5;
			P0.Priority = 0;
			P3.Priority = 3;
			FSoundHandle H5 = Limited.Play( A, P5 );
			FSoundHandle H0 = Limited.Play( A, P0 );
			FSoundHandle H3 = Limited.Play( A, P3 );
			FSoundPlayParams P4;
			P4.Priority = 4;
			FSoundHandle H4 = Limited.Play( A, P4 );
			C.Check( H4.IsValid() && Limited.IsPlaying( H5 ) && !Limited.IsPlaying( H0 ) && Limited.IsPlaying( H3 ), "the lowest priority voice is the one that is stopped" );
			FSoundPlayParams P1;
			P1.Priority = 1;
			C.Check( !Limited.Play( A, P1 ).IsValid() && Limited.IsPlaying( H3 ) && Limited.IsPlaying( H4 ), "a sound that ranks below every playing voice is rejected" );
			Limited.StopAll();
			Limited.SetBackendOverride( nullptr );
		}

		// 同じ音の数
		{
			FSoundPlayParams Two;
			Two.MaxInstances = 2;
			FSoundHandle First = Audio.Play( A, Two );
			Audio.Update( 0.1f );
			FSoundHandle Second = Audio.Play( A, Two );
			Audio.Update( 0.1f );
			FSoundHandle Third = Audio.Play( A, Two );
			C.Check( Third.IsValid(), "the new sound is played" );
			C.Check( !Audio.IsPlaying( First ) && Audio.IsPlaying( Second ) && Audio.IsPlaying( Third ), "MaxInstances stops the oldest" );
			Audio.Update( 0.05f );
			C.Check( Audio.GetNumVoices() == 2, "the oldest is gone after its short fade" );

			FSoundPlayParams Ignore;
			Ignore.MaxInstances = 2;
			Ignore.Overflow = EAudioOverflow::IgnoreNew;
			C.Check( !Audio.Play( A, Ignore ).IsValid(), "IgnoreNew rejects the new sound" );
			Audio.StopAll();

			FSoundPlayParams Interval;
			Interval.MinInterval = 0.5f;
			C.Check( Audio.Play( Chirp, Interval ).IsValid() && !Audio.Play( Chirp, Interval ).IsValid(), "MinInterval rejects a retrigger" );
			Audio.Update( 0.6f );
			C.Check( Audio.Play( Chirp, Interval ).IsValid(), "and allows it after the interval" );
			Audio.StopAll();
		}

		// 音楽のクロスフェード（等パワー）
		{
			FSoundHandle Music1 = Audio.PlayMusic( M1 );
			const int32 Voice1 = Rec.LastStarted;
			C.Check( Music1.IsValid() && Audio.GetCurrentMusic() == Music1 && Rec.Voices[Voice1].bLoop, "music plays, looping" );
			Audio.Update( 1.0f );
			C.Near( Rec.Voices[Voice1].Volume, 1.0, 1.0e-4, "music faded in" );
			C.Check( Audio.PlayMusic( M1 ) == Music1, "asking for the same music keeps it playing" );

			FMusicParams Cross;
			Cross.CrossFadeTime = 2.0f;
			FSoundHandle Music2 = Audio.PlayMusic( M2, Cross );
			const int32 Voice2 = Rec.LastStarted;
			C.Check( Music2.IsValid() && Music2 != Music1 && Audio.GetCurrentMusic() == Music2, "the new music replaces the old" );
			Audio.Update( 1.0f );
			C.Near( Rec.Voices[Voice1].Volume, 0.70711, 1.0e-3, "crossfade halfway: the old music is at cos(45 deg)" );
			C.Near( Rec.Voices[Voice2].Volume, 0.70711, 1.0e-3, "crossfade halfway: the new music is at sin(45 deg)" );
			Audio.Update( 1.0f );
			C.Check( !Rec.Voices[Voice1].bPlaying && Rec.Voices[Voice2].bPlaying, "after the crossfade only the new music plays" );
			C.Near( Rec.Voices[Voice2].Volume, 1.0, 1.0e-4, "at full volume" );

			Audio.StopMusic( 1.0f );
			Audio.Update( 1.1f );
			C.Check( Audio.GetNumVoices() == 0 && !Audio.GetCurrentMusic().IsValid(), "StopMusic fades out and stops" );
			C.Check( M1->GetNumVoicesInUse() == 0 && M2->GetNumVoicesInUse() == 0, "stream voices are returned" );

			// 同じ Stream の音は同時に 2 つ鳴らせない
			FSoundHandle S1 = Audio.Play( M1 );
			C.Check( S1.IsValid() && !Audio.Play( M1 ).IsValid(), "a stream sound cannot play twice at once" );
			Audio.StopAll();
		}

		// 環境音
		{
			FAmbientParams Wind;
			Wind.FadeInTime = 2.0f;
			Wind.Volume = 0.6f;
			FSoundHandle Ambient = Audio.PlayAmbient( A, Wind );
			const int32 AmbientVoice = Rec.LastStarted;
			C.Check( Ambient.IsValid() && Rec.Voices[AmbientVoice].bLoop, "ambient sounds loop" );
			Audio.SetBusVolume( EAudioBus::Ambient, 0.5f );
			Audio.Update( 2.0f );
			C.Near( Rec.Voices[AmbientVoice].Volume, 0.3, 1.0e-4, "ambient volume = sound 0.6 x ambient bus 0.5" );
			Audio.SetBusVolume( EAudioBus::Ambient, 1.0f );
			Audio.StopBus( EAudioBus::Ambient );
			C.Check( Audio.GetNumVoices() == 0, "StopBus" );
		}

		// 一時停止（止まっている間はフェードも止まる）
		{
			FSoundPlayParams Pausable;
			Pausable.FadeInTime = 2.0f;
			FSoundPlayParams Ui;
			Ui.bPausable = false;
			Ui.Bus = EAudioBus::Ui;
			FSoundHandle Game = Audio.Play( A, Pausable );
			const int32 GameVoice = Rec.LastStarted;
			FSoundHandle Menu = Audio.Play( B, Ui );
			const int32 MenuVoice = Rec.LastStarted;
			Audio.Update( 0.5f );
			C.Near( Rec.Voices[GameVoice].Volume, 0.25, 1.0e-4, "fade-in before the pause" );
			Audio.SetPaused( true );
			C.Check( !Rec.Voices[GameVoice].bPlaying && Rec.Voices[MenuVoice].bPlaying, "pause stops pausable voices only" );
			Audio.Update( 1.0f );
			C.Near( Rec.Voices[GameVoice].Volume, 0.25, 1.0e-4, "fades do not advance while paused" );
			C.Check( Audio.IsPlaying( Game ) && Audio.IsPlaying( Menu ), "paused voices are still alive" );
			Audio.SetPaused( false );
			C.Check( Rec.Voices[GameVoice].bPlaying && Rec.Voices[GameVoice].ResumeCalls == 1, "resume continues from where it stopped" );
			Audio.Update( 0.5f );
			C.Near( Rec.Voices[GameVoice].Volume, 0.5, 1.0e-4, "the fade continues after resuming" );

			Audio.Pause( Game );
			C.Check( !Rec.Voices[GameVoice].bPlaying, "a single voice can be paused" );
			Audio.Resume( Game );
			C.Check( Rec.Voices[GameVoice].bPlaying, "and resumed" );
			Audio.SetBusPaused( EAudioBus::Sfx, true );
			C.Check( !Rec.Voices[GameVoice].bPlaying && Rec.Voices[MenuVoice].bPlaying, "a bus can be paused" );
			Audio.SetBusPaused( EAudioBus::Sfx, false );
			Audio.StopAll();
		}

		// 途中から始める・読み込み中の音
		{
			FSoundPlayParams From;
			From.StartTime = 0.75f;
			FSoundHandle Offset = Audio.Play( A, From );
			C.Near( Rec.Voices[Rec.LastStarted].Position, 0.75, 1.0e-6, "StartTime sets the play position" );
			Audio.Stop( Offset );

			F.Touch( "late.wav" );
			TSharedPtr<CSound> Late = Assets.LoadSound( "late.wav", ESoundLoadMode::Memory, true );
			C.Check( Late.IsValid() && !Late->IsReady() && !Audio.Play( Late ).IsValid(), "a sound that is still loading cannot be played" );
			F.Fake->FinishLoading();
			C.Check( Late->IsReady() && Audio.Play( Late ).IsValid(), "and can be played once loaded" );
			Audio.StopAll();
			C.Check( !Audio.Play( nullptr ).IsValid(), "a null sound is rejected" );
		}

		Audio.StopAll();
		Audio.SetBackendOverride( nullptr );
	}

	//=========================================================================
	// 6. 描画コマンド
	//=========================================================================
	void TestDrawCommands( FChecker& C )
	{
		C.Group = "draw-commands";

		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Draw.SpriteRect( 10.0f, 20.0f, 7, 1, 2, 3, 4, 1.5f, 2.5f, 2.0f, 3.0f, 0.5f, true, false );
		Draw.Tint( FColor32( 10, 20, 30 ) );
		Draw.TextWithFont( 5, 100, 200, "Hello", FColor32( 1, 2, 3 ), EDrawTextAlign::Center, FColor32( 4, 5, 6 ) );
		Draw.ModelAnimLayer( 9, 2, 12.5f, 0.25f );

		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		C.Check( Log.Count( EDrawCommand::SpriteRect ) == 1 && Log.Count( EDrawCommand::Bright ) == 1 && Log.Count( EDrawCommand::TextFont ) == 1 && Log.Count( EDrawCommand::ModelAnim ) == 1,
				 "each new command is recorded once" );
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			switch ( Command.Type )
			{
			case EDrawCommand::SpriteRect:
			{
				const FDrawSpriteRectData D = Command.Read<FDrawSpriteRectData>();
				C.Check( D.X == 10.0f && D.Y == 20.0f && D.Handle == 7 && D.SrcX == 1 && D.SrcY == 2 && D.SrcW == 3 && D.SrcH == 4, "SpriteRect fields" );
				C.Check( D.PivotX == 1.5f && D.PivotY == 2.5f && D.ScaleX == 2.0f && D.ScaleY == 3.0f && D.Angle == 0.5f && Command.Has( FDrawFlag::FlipX ) && !Command.Has( FDrawFlag::FlipY ), "SpriteRect pivot, scale, angle and flip" );
				break;
			}
			case EDrawCommand::Bright:
			{
				const FDrawBrightData D = Command.Read<FDrawBrightData>();
				C.Check( D.Color.R == 10 && D.Color.G == 20 && D.Color.B == 30, "Bright color" );
				break;
			}
			case EDrawCommand::TextFont:
			{
				const FDrawTextFontData D = Command.Read<FDrawTextFontData>();
				C.Check( D.X == 100 && D.Y == 200 && D.Font == 5 && D.Length == 5 && D.Color.R == 1 && D.EdgeColor.B == 6, "TextFont fields" );
				C.Check( std::strcmp( Command.GetFontTextChars(), "Hello" ) == 0 && Command.Has( FDrawFlag::AlignCenter ) && !Command.Has( FDrawFlag::AlignRight ), "TextFont text and alignment" );
				break;
			}
			case EDrawCommand::ModelAnim:
			{
				const FDrawModelAnimData D = Command.Read<FDrawModelAnimData>();
				C.Check( D.Handle == 9 && D.AttachIndex == 2 && D.Time == 12.5f && D.Rate == 0.25f, "ModelAnim fields" );
				break;
			}
			default: break;
			}
		} );

		// 同じ呼び出しなら、1 ワードも違わない並びになる
		FDrawBuffer Again;
		FDrawContext AgainDraw( Again );
		AgainDraw.SpriteRect( 10.0f, 20.0f, 7, 1, 2, 3, 4, 1.5f, 2.5f, 2.0f, 3.0f, 0.5f, true, false );
		AgainDraw.Tint( FColor32( 10, 20, 30 ) );
		AgainDraw.TextWithFont( 5, 100, 200, "Hello", FColor32( 1, 2, 3 ), EDrawTextAlign::Center, FColor32( 4, 5, 6 ) );
		AgainDraw.ModelAnimLayer( 9, 2, 12.5f, 0.25f );
		C.Check( Again.Words == Buffer.Words, "the same calls give the same words (bit-comparable streams)" );

		// 長すぎる文字列は切る（ヘッダの 16bit に収まる）
		FDrawBuffer LongBuffer;
		FDrawContext LongDraw( LongBuffer );
		const std::string Long( 300000, 'x' );
		LongDraw.TextWithFont( 1, 0, 0, Long.c_str(), FColor32::White );
		FCommandLog LongLog;
		LongLog.Execute( FDrawSpan { LongBuffer.Words.data(), static_cast<int32>( LongBuffer.Words.size() ) } );
		C.Check( LongLog.Count( EDrawCommand::TextFont ) == 1 && LongBuffer.Words.size() <= static_cast<size_t>( NDrawCommand::MaxWords ), "a very long string is truncated to fit one command" );
		FDrawBuffer EmptyBuffer;
		FDrawContext EmptyDraw( EmptyBuffer );
		EmptyDraw.TextWithFont( 1, 0, 0, nullptr, FColor32::White );
		FCommandLog EmptyLog;
		EmptyLog.Execute( FDrawSpan { EmptyBuffer.Words.data(), static_cast<int32>( EmptyBuffer.Words.size() ) } );
		EmptyLog.ForEach( [&]( const FDrawCommandView& Command ) { C.Check( Command.Type == EDrawCommand::TextFont && Command.Read<FDrawTextFontData>().Length == 0, "a null string is an empty string" ); } );
	}


	//=========================================================================
	// 7. データアセットの編集（エディタの操作）
	//=========================================================================
	FSpriteSheetDefinition MakeSheetDefinition()
	{
		FSpriteSheetDefinition Definition;
		Definition.TexturePath = "a.png";
		FSpriteSheetGrid Grid;
		Grid.Columns = 4;
		Grid.Rows = 2;
		Definition.Grids.Add( Grid );
		Definition.Frames.Add( FSpriteFrame { 0, 40, 20, 10, 10.0f, 5.0f } );   // 番号 8
		Definition.Clips.Add( FSpriteClip::FromRange( "run", 0, 3, 10.0f ).AddEvent( 2, "Step" ).AddEvent( 0, "Start" ) );
		Definition.Clips.Add( FSpriteClip::FromFrames( "free", { 8, 1 }, 10.0f ) );
		return Definition;
	}

	void TestDocument( FChecker& C )
	{
		C.Group = "document";
		TSpriteSheetDocument Document;
		Document.Reset( MakeSheetDefinition() );
		C.Check( !Document.IsDirty() && !Document.CanUndo() && !Document.CanRedo(), "a fresh document is clean" );

		const uint32 Revision = Document.GetRevision();
		NSpriteSheetEdit::SetTexturePath( Document, "b.png" );
		C.Check( Document.IsDirty() && Document.CanUndo() && Document.Get().TexturePath == "b.png" && Document.GetRevision() != Revision, "an edit marks the document dirty" );
		C.Check( Document.Undo() && Document.Get().TexturePath == "a.png" && !Document.IsDirty() && Document.CanRedo(), "undo restores the value and the clean state" );
		C.Check( Document.Redo() && Document.Get().TexturePath == "b.png" && Document.IsDirty(), "redo re-applies it" );
		Document.Undo();
		NSpriteSheetEdit::SetTexturePath( Document, "c.png" );
		C.Check( !Document.CanRedo(), "a new edit clears the redo history" );
		C.Check( Document.Undo() && Document.Get().TexturePath == "a.png", "undo goes back to the original" );
		C.Check( !Document.Undo(), "and stops at the beginning" );

		// 連続した変更は 1 回の編集
		for ( int32 i = 1; i <= 5; ++i ) NSpriteSheetEdit::SetClipFps( Document, 0, 10.0f + static_cast<float32>( i ), "fps" );
		C.Near( Document.Get().Clips[0].Fps, 15.0, 1.0e-6, "the last value of a drag is kept" );
		C.Check( Document.GetNumUndo() == 1, "five coalesced changes are one undo step (got %d)", Document.GetNumUndo() );
		Document.EndEditSession();
		NSpriteSheetEdit::SetClipFps( Document, 0, 30.0f, "fps" );
		C.Check( Document.GetNumUndo() == 2, "a new session is a new undo step" );
		Document.Undo();
		C.Near( Document.Get().Clips[0].Fps, 15.0, 1.0e-6, "undo goes back one session" );
		Document.Undo();
		C.Near( Document.Get().Clips[0].Fps, 10.0, 1.0e-6, "and then to the original" );

		// 保存の印
		NSpriteSheetEdit::SetTexturePath( Document, "saved.png" );
		Document.MarkSaved();
		C.Check( !Document.IsDirty(), "saving clears the dirty mark" );
		NSpriteSheetEdit::SetTexturePath( Document, "after-save.png" );
		C.Check( Document.IsDirty(), "and an edit after saving sets it again" );
		Document.Undo();
		C.Check( !Document.IsDirty(), "undoing back to the saved state is clean again" );
	}

	void TestSpriteEditing( FChecker& C )
	{
		C.Group = "sprite-editing";
		TSpriteSheetDocument Doc;
		Doc.Reset( MakeSheetDefinition() );
		const auto Frames = [&]( int32 Clip ) { return Doc.Get().Clips[Clip].Frames; };
		C.Check( Doc.Get().GetNumFrames() == 9 && Doc.Get().GetNumGridFrames() == 8, "9 frames: 8 from the grid, 1 free" );

		// 格子を足すと、自由な矩形の番号がずれて、クリップも一緒に直る
		FSpriteSheetGrid Second;
		Second.Columns = 2;
		Second.Rows = 2;
		NSpriteSheetEdit::AddGrid( Doc, Second );
		C.Check( Doc.Get().GetNumFrames() == 13 && Frames( 1 ) == TArray<int32> { 12, 1 }, "adding a 2x2 grid moves the free frame to 12 and the clip follows" );
		NSpriteSheetEdit::RemoveGrid( Doc, 1 );
		C.Check( Doc.Get().GetNumFrames() == 9 && Frames( 1 ) == TArray<int32> { 8, 1 }, "removing it moves the free frame back to 8" );

		// 格子の大きさを変える（縮めると、はみ出したコマは外れる）
		FSpriteSheetGrid Shrunk = Doc.Get().Grids[0];
		Shrunk.NumFrames = 4;
		NSpriteSheetEdit::SetGrid( Doc, 0, Shrunk );
		C.Check( Doc.Get().GetNumFrames() == 5 && Frames( 1 ) == TArray<int32> { 4, 1 }, "shrinking the grid to 4 cells moves the free frame to 4" );
		Shrunk.NumFrames = 8;
		NSpriteSheetEdit::SetGrid( Doc, 0, Shrunk );
		Doc.Undo();
		Doc.Undo();
		C.Check( Doc.Get().Grids[0].GetNumFrames() == 8 && Frames( 1 ) == TArray<int32> { 8, 1 }, "undo brings the clip's frame numbers back as well" );

		// 格子を消すと、その格子のコマを使っていたステップは外れ、出来事は動く
		TSpriteSheetDocument Two;
		FSpriteSheetDefinition TwoDefinition = MakeSheetDefinition();
		FSpriteSheetGrid Extra;
		Extra.Columns = 2;
		Extra.Rows = 1;
		TwoDefinition.Grids.Add( Extra );   // 番号 8, 9。自由な矩形は 10
		TwoDefinition.Clips.Add( FSpriteClip::FromFrames( "mix", { 0, 8, 9, 10 }, 10.0f ).AddEvent( 1, "A" ).AddEvent( 3, "B" ) );
		Two.Reset( TwoDefinition );
		NSpriteSheetEdit::RemoveGrid( Two, 1 );
		C.Check( Two.Get().Clips[2].Frames == TArray<int32> { 0, 8 }, "removing grid 1 drops its steps and renumbers the free frame (got %d steps)", Two.Get().Clips[2].Frames.Num() );
		C.Check( Two.Get().Clips[2].Events.Num() == 1 && Two.Get().Clips[2].Events[0].Name == "B" && Two.Get().Clips[2].Events[0].Step == 1, "the event of a dropped step goes; the other follows its step" );

		// 自由な矩形
		const int32 Added = NSpriteSheetEdit::AddFrame( Doc, FSpriteFrame { 1, 2, 3, 4, 0.0f, 0.0f } );
		C.Check( Added == 9 && Doc.Get().Frames.Num() == 2, "AddFrame returns the frame number" );
		NSpriteSheetEdit::SetFrame( Doc, 3, FSpriteFrame { 99, 99, 9, 9, 0, 0 } );   // 格子のコマは変えられない
		C.Check( Doc.Get().Frames[0].X == 0, "a grid cell cannot be edited as a free frame" );
		NSpriteSheetEdit::SetFrame( Doc, 9, FSpriteFrame { 7, 8, 9, 10, 1.0f, 2.0f } );
		C.Check( Doc.Get().Frames[1].X == 7 && Doc.Get().Frames[1].PivotY == 2.0f, "a free frame can be edited" );
		NSpriteSheetEdit::AppendStep( Doc, 1, 9 );
		NSpriteSheetEdit::RemoveFrame( Doc, 8 );
		C.Check( Doc.Get().Frames.Num() == 1 && Frames( 1 ) == TArray<int32> { 1, 8 }, "removing free frame 8 drops its step and renumbers frame 9 -> 8" );

		// クリップ
		const int32 Clip = NSpriteSheetEdit::AddClip( Doc, "run" );
		C.Check( Doc.Get().Clips[Clip].Name == "run 2" && Doc.Get().Clips[Clip].Frames.Num() == 1, "a clip name that is taken gets a number; a clip starts with one frame" );
		C.Check( NSpriteSheetEdit::RenameClip( Doc, Clip, "run" ) == "run 2" && NSpriteSheetEdit::RenameClip( Doc, Clip, "run 2" ) == "run 2", "renaming to a taken name keeps it unique; the own name is fine" );
		C.Check( NSpriteSheetEdit::RenameClip( Doc, Clip, "   " ) == "Clip", "an empty name becomes 'Clip'" );
		NSpriteSheetEdit::RemoveClip( Doc, Clip );
		C.Check( Doc.Get().Clips.Num() == 2, "RemoveClip" );
		NSpriteSheetEdit::SetClipLoop( Doc, 0, EAnimLoopMode::PingPong );
		C.Check( Doc.Get().Clips[0].Loop == EAnimLoopMode::PingPong, "SetClipLoop" );

		// ステップと出来事（run: 0 1 2 3、出来事 Start@0, Step@2）
		NSpriteSheetEdit::InsertStep( Doc, 0, 1, 7 );
		C.Check( Frames( 0 ) == TArray<int32> { 0, 7, 1, 2, 3 } && Doc.Get().Clips[0].Events[0].Step == 3 && Doc.Get().Clips[0].Events[1].Step == 0, "inserting a step shifts the events after it" );
		NSpriteSheetEdit::RemoveStep( Doc, 0, 1 );
		C.Check( Frames( 0 ) == TArray<int32> { 0, 1, 2, 3 } && Doc.Get().Clips[0].Events[0].Step == 2, "removing it restores them" );
		NSpriteSheetEdit::RemoveStep( Doc, 0, 3 );
		NSpriteSheetEdit::MoveStep( Doc, 0, 0, 2 );   // 0 1 2 -> 1 2 0
		C.Check( Frames( 0 ) == TArray<int32> { 1, 2, 0 }, "MoveStep reorders the steps" );
		int32 StartStep = -1, StepStep = -1;
		for ( const FSpriteAnimEvent& Event : Doc.Get().Clips[0].Events )
		{
			if ( Event.Name == "Start" ) StartStep = Event.Step;
			if ( Event.Name == "Step" ) StepStep = Event.Step;
		}
		C.Check( StartStep == 2 && StepStep == 1, "events follow their steps when one is moved (Start 0 -> 2, Step 2 -> 1; got %d, %d)", StartStep, StepStep );
		NSpriteSheetEdit::MoveStep( Doc, 0, 2, 0 );
		C.Check( Frames( 0 ) == TArray<int32> { 0, 1, 2 } && Doc.Get().Clips[0].Events[0].Step == 2 && Doc.Get().Clips[0].Events[1].Step == 0, "and back" );

		NSpriteSheetEdit::SetStepFrame( Doc, 0, 1, 6 );
		C.Check( Frames( 0 )[1] == 6, "SetStepFrame" );
		NSpriteSheetEdit::SetStepRange( Doc, 0, 2, 5 );
		C.Check( Frames( 0 ) == TArray<int32> { 2, 3, 4, 5 } && Doc.Get().Clips[0].Events.Num() == 2, "SetStepRange replaces the steps and keeps the events that still fit" );
		NSpriteSheetEdit::SetStepRange( Doc, 0, 2, 3 );
		C.Check( Doc.Get().Clips[0].Events.Num() == 1, "a shorter range drops the events past the end" );

		// 表示時間
		NSpriteSheetEdit::SetStepDuration( Doc, 0, 1, 0.5f );
		C.Check( Doc.Get().Clips[0].Durations.Num() == 2 && Doc.Get().Clips[0].Durations[1] == 0.5f, "a custom duration creates the list" );
		NSpriteSheetEdit::SetStepDuration( Doc, 0, 1, 0.1f );
		C.Check( Doc.Get().Clips[0].Durations.IsEmpty(), "setting it back to the default removes the list" );
		NSpriteSheetEdit::SetStepDuration( Doc, 0, 0, 0.3f );
		NSpriteSheetEdit::ResetDurations( Doc, 0 );
		C.Check( Doc.Get().Clips[0].Durations.IsEmpty(), "ResetDurations" );

		// 出来事
		const int32 Event = NSpriteSheetEdit::AddEvent( Doc, 0, 99, "Late" );
		C.Check( Event == Doc.Get().Clips[0].Events.Num() - 1 && Doc.Get().Clips[0].Events[Event].Step == 1, "an event past the end is put on the last step" );
		NSpriteSheetEdit::SetEvent( Doc, 0, Event, 0, "Early" );
		C.Check( Doc.Get().Clips[0].Events[Event].Step == 0 && Doc.Get().Clips[0].Events[Event].Name == "Early", "SetEvent" );
		NSpriteSheetEdit::RemoveEvent( Doc, 0, Event );
		C.Check( Doc.Get().Clips[0].Events.Num() == 1, "RemoveEvent" );

		// 確認
		FSpriteSheetDefinition Broken;
		Broken.Grids.Add( FSpriteSheetGrid { 0, 0 } );
		Broken.Clips.Add( FSpriteClip::FromRange( "a", 0, 1 ) );
		Broken.Clips.Add( FSpriteClip::FromRange( "a", 0, 1 ) );
		Broken.Clips.Add( FSpriteClip::FromFrames( "oob", { 0, 50 }, 10.0f ).AddEvent( 5, "Nowhere" ) );
		FSpriteClip Empty;
		Empty.Name = "empty";
		Broken.Clips.Add( Empty );
		const TArray<FString> Problems = NSpriteSheetEdit::Validate( Broken, 0, 0 );
		const auto Mentions = [&]( const char* Text ) { return Problems.ContainsByPredicate( [Text]( const FString& Problem ) { return Problem.Contains( Text ); } ); };
		C.Check( Mentions( "No texture" ) && Mentions( "at least 1" ) && Mentions( "more than once" ) && Mentions( "frame 50" ) && Mentions( "does not exist" ) && Mentions( "no frames" ),
				 "validation finds the missing texture, bad grid, duplicate name, bad frame, bad event and empty clip (%d problem(s))", Problems.Num() );
		FSpriteSheetDefinition Fits = MakeSheetDefinition();
		C.Check( NSpriteSheetEdit::Validate( Fits, 128, 64 ).IsEmpty(), "a valid definition has no problems" );
		FSpriteSheetGrid TooWide = Fits.Grids[0];
		TooWide.CellWidth = 50;
		Fits.Grids[0] = TooWide;
		C.Check( NSpriteSheetEdit::Validate( Fits, 128, 64 ).ContainsByPredicate( []( const FString& Problem ) { return Problem.Contains( "outside the texture" ); } ), "cells that do not fit in the texture are reported" );
	}

	void TestModelEditing( FChecker& C )
	{
		C.Group = "model-editing";
		TModelAnimationDocument Doc;
		FModelAnimationDefinition Definition;
		Definition.ModelPath = "robot.mv1";
		Doc.Reset( Definition );

		FModelClipDef Walk;
		Walk.Name = "Walk";
		C.Check( NModelAnimationEdit::AddClip( Doc, Walk ) == 0 && NModelAnimationEdit::AddClip( Doc, Walk ) == 1 && Doc.Get().Clips[1].Name == "Walk 2", "clip names stay unique" );
		FModelClipDef Run;
		Run.Name = "Run";
		NModelAnimationEdit::AddClip( Doc, Run );
		NModelAnimationEdit::MoveClip( Doc, 2, 0 );
		C.Check( Doc.Get().Clips[0].Name == "Run" && Doc.Get().Clips[1].Name == "Walk", "MoveClip" );
		C.Check( NModelAnimationEdit::RenameClip( Doc, 0, "Walk" ) == "Walk 3", "renaming to a taken name is numbered" );
		NModelAnimationEdit::RemoveClip( Doc, 2 );
		C.Check( Doc.Get().Clips.Num() == 2, "RemoveClip" );

		FModelClipDef Settings;
		Settings.SourcePath = "walk.mv1";
		Settings.AnimIndex = 3;
		Settings.AnimName = "mixamo.com";
		Settings.Loop = EAnimLoopMode::Once;
		Settings.Speed = 2.0f;
		Settings.FadeTime = -1.0f;
		Settings.Name = "ignored";
		Settings.Events.Add( FModelEventDef { 0.5f, "ignored" } );
		NModelAnimationEdit::SetClipSettings( Doc, 0, Settings );
		const FModelClipDef& Edited = Doc.Get().Clips[0];
		C.Check( Edited.SourcePath == "walk.mv1" && Edited.AnimIndex == 3 && Edited.AnimName == "mixamo.com" && Edited.Loop == EAnimLoopMode::Once && Edited.Speed == 2.0f && Edited.FadeTime == 0.0f,
				 "SetClipSettings (a negative fade becomes 0)" );
		C.Check( Edited.Name == "Walk 3" && Edited.Events.IsEmpty(), "and leaves the name and the events alone" );

		// 出来事は時刻の順
		C.Check( NModelAnimationEdit::AddEvent( Doc, 0, 0.8f, "B" ) == 0, "the first event is index 0" );
		C.Check( NModelAnimationEdit::AddEvent( Doc, 0, 0.2f, "A" ) == 0, "an earlier event is put before it" );
		C.Check( NModelAnimationEdit::AddEvent( Doc, 0, 0.5f, "M" ) == 1, "and a middle one in between" );
		C.Check( NModelAnimationEdit::AddEvent( Doc, 0, 5.0f, "Z" ) == 3 && Doc.Get().Clips[0].Events[3].Time == 1.0f, "a time past 1 is clamped to 1" );
		const int32 Moved = NModelAnimationEdit::SetEvent( Doc, 0, 0, 0.9f, "A" );
		C.Check( Moved == 2 && Doc.Get().Clips[0].Events[2].Name == "A", "moving an event past others re-sorts and returns its new index (got %d)", Moved );
		float32 Previous = -1.0f;
		bool bSorted = true;
		for ( const FModelEventDef& Event : Doc.Get().Clips[0].Events )
		{
			bSorted = bSorted && Event.Time >= Previous;
			Previous = Event.Time;
		}
		C.Check( bSorted, "events stay sorted by time" );
		NModelAnimationEdit::RemoveEvent( Doc, 0, 0 );
		C.Check( Doc.Get().Clips[0].Events.Num() == 3, "RemoveEvent" );

		NModelAnimationEdit::SetPlacement( Doc, FVector3( 1, 2, 3 ), FVector3( 0, 90, 0 ), FVector3( 2, 2, 2 ) );
		NModelAnimationEdit::SetUnitsPerSecond( Doc, 24.0f );
		C.Check( Doc.Get().Offset.z == 3.0f && Doc.Get().RotationDegrees.y == 90.0f && Doc.Get().Scale.x == 2.0f && Doc.Get().UnitsPerSecond == 24.0f, "placement and units per second" );
		C.Check( RoundTrips( Doc.Get() ), "an edited definition survives JSON" );

		// 元に戻す
		for ( int32 i = 0; i < 20 && Doc.Undo(); ++i ) {}
		C.Check( Doc.Get().Clips.IsEmpty() && !Doc.IsDirty(), "undoing everything returns to the empty definition" );

		FModelAnimationDefinition Bad;
		FModelClipDef A, B;
		A.Name = "A";
		A.AnimIndex = -1;
		A.Speed = 0.0f;
		A.Events.Add( FModelEventDef { 2.0f, "" } );
		B.Name = "A";
		Bad.UnitsPerSecond = 0.0f;
		Bad.Clips.Add( A );
		Bad.Clips.Add( B );
		const TArray<FString> Problems = NModelAnimationEdit::Validate( Bad );
		const auto Mentions = [&]( const char* Text ) { return Problems.ContainsByPredicate( [Text]( const FString& Problem ) { return Problem.Contains( Text ); } ); };
		C.Check( Mentions( "no animation file" ) && Mentions( "Units per second" ) && Mentions( "more than once" ) && Mentions( "neither" ) && Mentions( "speed 0" ) && Mentions( "without a name" ) && Mentions( "outside 0..1" ),
				 "validation finds every kind of problem (%d problem(s))", Problems.Num() );

		// 何もない定義と、モデルを持たない集（クリップのファイルが揃っていれば問題なし）
		const TArray<FString> EmptyProblems = NModelAnimationEdit::Validate( FModelAnimationDefinition() );
		C.Check( EmptyProblems.Num() == 1 && EmptyProblems[0].Contains( "No model" ), "an empty definition says it has neither a model nor clips" );
		FModelAnimationDefinition Library;
		FModelClipDef FromFile;
		FromFile.Name = "Walk";
		FromFile.SourcePath = "walk.mv1";
		Library.Clips.Add( FromFile );
		C.Check( NModelAnimationEdit::Validate( Library ).IsEmpty(), "a set without a model is valid when every clip names its animation file" );
		Library.Clips.Add( B );
		C.Check( NModelAnimationEdit::Validate( Library ).ContainsByPredicate( []( const FString& Problem ) { return Problem.Contains( "no animation file, and the set has no model" ); } ),
				 "a clip without an animation file is a problem when the set has no model" );
	}

	// 格子の計算は、スプライトシートが実際に使うものと、エディタが重ねる枠で同じ
	void TestGridComputation( FChecker& C, FFixture& F )
	{
		C.Group = "grid-computation";
		F.Touch( "grid.png" );
		F.Fake->TextureWidth = 120;
		F.Fake->TextureHeight = 90;
		TSharedPtr<CTexture> Texture = F.Assets->LoadTexture( "grid.png" );

		FSpriteSheetGrid Grids[3];
		Grids[0].Columns = 4; Grids[0].Rows = 3;
		Grids[1].Columns = 3; Grids[1].Rows = 2; Grids[1].CellWidth = 30; Grids[1].CellHeight = 20; Grids[1].MarginX = 5; Grids[1].MarginY = 4; Grids[1].SpacingX = 2; Grids[1].SpacingY = 3;
		Grids[1].Order = ESpriteGridOrder::ColumnMajor;
		Grids[2].Columns = 5; Grids[2].Rows = 2; Grids[2].NumFrames = 7; Grids[2].bCustomPivot = true; Grids[2].PivotX = 2.0f; Grids[2].PivotY = 9.0f;
		bool bSame = true;
		for ( const FSpriteSheetGrid& Grid : Grids )
		{
			TSharedPtr<CSpriteSheet> Sheet = F.Assets->CreateSpriteSheet( Texture, Grid );
			TArray<FSpriteFrame> Computed;
			CSpriteSheet::ComputeGridFrames( Grid, 120, 90, Computed );
			bSame = bSame && Computed.Num() == Sheet->GetNumFrames();
			for ( int32 i = 0; bSame && i < Computed.Num(); ++i )
			{
				const FSpriteFrame* Frame = Sheet->GetFrame( i );
				bSame = Frame != nullptr && Frame->X == Computed[i].X && Frame->Y == Computed[i].Y && Frame->Width == Computed[i].Width && Frame->Height == Computed[i].Height
						&& Frame->PivotX == Computed[i].PivotX && Frame->PivotY == Computed[i].PivotY;
			}
		}
		C.Check( bSame, "ComputeGridFrames gives the same rectangles as the sprite sheet uses" );
	}

	//=========================================================================
	// 全部まとめて
	//=========================================================================
}

void NAssetTest::RunLogicTests( FChecker& C )
{
	TestTimeline( C );
	TestEntered( C );
	TestAudioMath( C );
	TestFontFile( C );
	TestDrawCommands( C );
	TestDocument( C );
	TestSpriteEditing( C );
	TestModelEditing( C );

	// 偽のバックエンドを差し込んだマネージャーでの確認
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		C.Check( false, "the asset manager subsystem does not exist" );
		return;
	}

	FFakeAssetBackend Fake;
	FFixture Fixture;
	Fixture.Assets = Assets;
	Fixture.Fake = &Fake;
	Fixture.Dir = MakeTempDirectory() / "logic";
	CreateDirectoryA( *Fixture.Dir, nullptr );

	const FString OldRoot = Assets->GetRootDirectory();
	Assets->SetBackendOverride( &Fake );
	Assets->SetRootDirectory( Fixture.Dir );

	TestSpriteSheet( C, Fixture );
	TestSpriteAnimator( C, Fixture );
	TestModelAnimator( C, Fixture );
	TestModelAnimationAsset( C, Fixture );
	TestAssetManager( C, Fixture );
	TestAudioSystem( C, Fixture );
	TestGridComputation( C, Fixture );
	RunImportLogicTests( C, *Assets, Fake, Fixture.Dir );

	// 作ったアセットがすべて手放されていること（偽のバックエンドを外す前に）
	Assets->ReleaseUnused();
	Assets->Tick( 0.0f );
	C.Group = "cleanup";
	C.Check( Fake.Textures.empty() && Fake.Models.empty() && Fake.Sounds.empty() && Fake.Fonts.empty(),
			 "every asset of the logic checks was released (textures %d, models %d, sounds %d, fonts %d)", static_cast<int32>( Fake.Textures.size() ), static_cast<int32>( Fake.Models.size() ),
			 static_cast<int32>( Fake.Sounds.size() ), static_cast<int32>( Fake.Fonts.size() ) );

	Assets->SetBackendOverride( nullptr );
	Assets->SetRootDirectory( "Assets" );
	(void)OldRoot;
	RemoveDirectoryRecursive( Fixture.Dir );
}
