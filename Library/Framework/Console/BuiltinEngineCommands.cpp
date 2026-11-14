// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: エンジンとウィンドウ（終了・時間の進め方・フルスクリーン・撮影など）
void NConsole::NBuiltin::RegisterEngineCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "fps", "Show the frame rate, the frame time and the frame count",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%.1f FPS, frame time %.2f ms, frame %llu", Platform->GetFPS(), Platform->GetDeltaTime() * 1000.0f, static_cast<unsigned long long>( Platform->GetFrameCount() ) );
		 },
		 {}, CategoryEngine );

	Add( "time", "Show the time since start, the time scale and whether the game is paused",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 CEngine* Engine = CEngine::Get();
			 if ( Platform == nullptr || Engine == nullptr ) return FString( NotRunning );
			 const FEngineTimeControl& Control = Engine->GetTimeControl();
			 return FString::Printf( "%.2f s since start, timescale %s, %s%s", Platform->GetTime(), *NConsole::FormatFloat( Control.GetScale() ), Control.IsPaused() ? "paused" : "running",
									 Control.GetStepsRemaining() > 0 ? *FString::Printf( " (%d step(s) left)", Control.GetStepsRemaining() ) : "" );
		 },
		 {}, CategoryEngine );

	Add( "pause", "Pause or resume the game (Tick gets a delta time of 0; drawing and the debug UI keep running)",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 FEngineTimeControl& Control = Engine->GetTimeControl();
			 const bool bPause = On.IsSet() ? On.GetValue() : !Control.IsPaused();
			 if ( bPause ) Control.Pause();
			 else Control.Resume();
			 Context.Print( bPause ? "paused (use 'step' to advance, 'resume' to continue)" : "resumed" );
			 return true;
		 },
		 { { "on", "true to pause, false to resume. Without it, switches." } }, CategoryEngine );

	Add( "resume", "Resume the game after pause",
		 []( FConsoleContext& Context )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 Engine->GetTimeControl().Resume();
			 Context.Print( "resumed" );
			 return true;
		 },
		 {}, CategoryEngine );

	Add( "step", "Advance the paused game by a few frames (1 frame = 1/60 s)",
		 []( FConsoleContext& Context, TOptional<int32> Frames )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 const int32 Count = Frames.IsSet() ? NMath::Max( Frames.GetValue(), 1 ) : 1;
			 Engine->GetTimeControl().Step( Count );
			 Context.Print( FString::Printf( "stepping %d frame(s)", Count ) );
			 return true;
		 },
		 { { "frames", "How many frames to advance (default 1). Also pauses the game." } }, CategoryEngine );

	Add( "jobs", "Show the number of job system worker threads",
		 []()
		 {
			 IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
			 return Jobs != nullptr ? FString::Printf( "%d worker thread(s)", Jobs->GetNumWorkers() ) : FString( NotRunning );
		 },
		 {}, CategoryEngine );

	Add( "memory", "Show the process memory, the video memory and the asset memory",
		 []()
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr ) return FString( NotRunning );
			 Assets->UpdateStats();
			 const FAssetStats& Stats = Assets->GetStats();
			 FString Text = FString::Printf( "process %s", *FormatBytes( Stats.Backend.ProcessWorkingSet ) );
			 if ( Stats.Backend.VideoMemoryTotal > 0 ) Text += FString::Printf( ", video %s / %s", *FormatBytes( Stats.Backend.VideoMemoryUsed ), *FormatBytes( Stats.Backend.VideoMemoryTotal ) );
			 Text += FString::Printf( ", assets ~%s (peak ~%s)", *FormatBytes( Stats.TotalMemoryBytes ), *FormatBytes( Stats.PeakMemoryBytes ) );
			 return Text;
		 },
		 {}, CategoryEngine );

	Add( "screenshot", "Save a screenshot of the current frame to Saved/Screenshots (Screenshot_00001.png when no name is given)",
		 []( FConsoleContext& Context, TOptional<FString> Name, TOptional<float32> Delay, TOptional<bool> bWithUI )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 CScreenCaptureSubsystem* Capture = GetAppSubsystem<CScreenCaptureSubsystem>();
			 if ( Platform == nullptr || Capture == nullptr )
			 {
				 Context.Error( "the screen capture is not available" );
				 return false;
			 }
			 FScreenshotOptions Options;
			 const float32 Seconds = Delay.IsSet() ? NMath::Max( Delay.GetValue(), 0.0f ) : 0.0f;
			 Options.DelayFrames = static_cast<int32>( Seconds * NMath::Max( Platform->GetFPS(), 1.0f ) + 0.5f );
			 // コンソールから撮るときは、既定でデバッグ用の UI も写す（コンソールを開いたままなので、見たままを残す）
			 Options.bIncludeDebugUI = bWithUI.IsSet() ? bWithUI.GetValue() : true;
			 const FString Path = Capture->TakeScreenshot( Name.IsSet() ? Name.GetValue() : FString(), Options );
			 if ( Path.IsEmpty() )
			 {
				 Context.Error( "a screenshot is already waiting to be taken" );
				 return false;
			 }
			 Context.Print( Options.DelayFrames > 0 ? FString::Printf( "saving %s in %s s (%d frame(s))", *Path, *NConsole::FormatFloat( Seconds ), Options.DelayFrames ) : "saving " + Path + " at the end of this frame" );
			 return true;
		 },
		 { { "name", "File name (.png / .jpg / .bmp / .gif). A relative name is placed under Saved/Screenshots. Default: Screenshot_<number>.png" },
		   { "delay", "Seconds to wait before taking it, so you can set the screen up (default 0)." },
		   { "ui", "Include the debug UI (default true from the console)." } }, CategoryEngine );

	Add( "gif", "Start or stop recording a GIF of the game screen to Saved/Screenshots (Shift + F9 does the same; F9 takes a screenshot)",
		 []( FConsoleContext& Context, TOptional<FString> Name, TOptional<int32> Step, TOptional<int32> Downscale, TOptional<bool> bWithUI )
		 {
			 CScreenCaptureSubsystem* Capture = GetAppSubsystem<CScreenCaptureSubsystem>();
			 if ( Capture == nullptr )
			 {
				 Context.Error( "the screen capture is not available" );
				 return false;
			 }
			 if ( Capture->IsRecording() )
			 {
				 const bool bSaved = Capture->StopRecording();
				 Context.Print( ( bSaved ? "saved " : "failed to save " ) + Capture->GetLastSavedPath() );
				 return bSaved;
			 }
			 FGifRecordOptions Options = Capture->GetHotkeyRecordOptions();
			 Options.bIncludeDebugUI = bWithUI.IsSet() ? bWithUI.GetValue() : Options.bIncludeDebugUI;
			 Options.FrameStep = Step.IsSet() ? Step.GetValue() : Options.FrameStep;
			 Options.Downscale = Downscale.IsSet() ? Downscale.GetValue() : Options.Downscale;
			 const FString Path = Capture->StartRecording( Name.IsSet() ? Name.GetValue() : FString(), Options );
			 Context.Print( "recording " + Path + " (run gif again to stop)" );
			 return !Path.IsEmpty();
		 },
		 { { "name", "File name. Default: Screenshot_<number>.gif" },
		   { "step", "Take one frame every N frames (default 2)." },
		   { "downscale", "Shrink by 1/N (default 1)." },
		   { "ui", "Include the debug UI (default false: the game screen only)." } }, CategoryEngine );

	Add( "highresshot", "Redraw the game at a larger size and save it to Saved/Screenshots (like Unreal's HighResShot). Optionally only the subject, with a transparent background",
		 []( FConsoleContext& Context, TOptional<FString> Size, TOptional<FString> Subject, TOptional<FString> Name )
		 {
			 CScreenCaptureSubsystem* Capture = GetAppSubsystem<CScreenCaptureSubsystem>();
			 if ( Capture == nullptr )
			 {
				 Context.Error( "the screen capture is not available" );
				 return false;
			 }
			 FHighResShotOptions Options;
			 const FString SizeText = Size.IsSet() ? Size.GetValue() : FString( "2" );
			 if ( !NCaptureCommandLine::ParseResolution( SizeText, Options.Width, Options.Height ) )
			 {
				 Options.Multiplier = SizeText.ToFloat( 0.0f );
			 }
			 if ( Subject.IsSet() && !Subject.GetValue().IsEmpty() )
			 {
				 Capture->SetSubjectByName( Subject.GetValue() );
				 Options.bSubjectOnly = true;
			 }
			 const FString Path = Capture->TakeHighResShot( Name.IsSet() ? Name.GetValue() : FString(), Options );
			 if ( Path.IsEmpty() )
			 {
				 Context.Error( "cannot take it (size 1..16384, or a shot is already waiting)" );
				 return false;
			 }
			 Context.Print( "saving " + Path );
			 return true;
		 },
		 { { "size", "A multiplier of the screen size (2 = twice) or a resolution such as 3840x2160. Default: 2" },
		   { "subject", "Only objects whose name contains this text are drawn; the rest becomes transparent (PNG)." },
		   { "name", "File name. Default: Screenshot_<number>.png" } }, CategoryEngine );

	Add( "window", "Show the window size, mode and title",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%d x %d, %s, title '%s'", Platform->GetScreenWidth(), Platform->GetScreenHeight(), WindowModeToString( Platform->GetWindowMode() ), *Platform->GetWindowTitle() );
		 },
		 {}, CategoryWindow );

	Add( "windowmode", "Show or change the window mode",
		 []( FConsoleContext& Context, TOptional<FString> Mode )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 if ( !Mode.IsSet() )
			 {
				 Context.Print( WindowModeToString( Platform->GetWindowMode() ) );
				 return true;
			 }
			 const TOptional<EWindowMode> Parsed = WindowModeFromString( Mode.GetValue() );
			 if ( !Parsed.IsSet() )
			 {
				 Context.Error( "unknown window mode '" + Mode.GetValue() + "' (windowed / borderless / fullscreen)" );
				 return false;
			 }
			 Platform->SetWindowMode( Parsed.GetValue() );
			 Context.Print( FString( "window mode: " ) + WindowModeToString( Platform->GetWindowMode() ) );
			 return true;
		 },
		 { { "mode", "windowed, borderless or fullscreen. Without it, shows the current mode." } }, CategoryWindow );
	Registry.SetArgChoices( "windowmode", 0, { "windowed", "borderless", "fullscreen" } );

	Add( "fullscreen", "Switch between the window and the last full screen mode",
		 []( FConsoleContext& Context )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 Platform->ToggleFullscreen();
			 Context.Print( FString( "window mode: " ) + WindowModeToString( Platform->GetWindowMode() ) );
			 return true;
		 },
		 {}, CategoryWindow );
}
