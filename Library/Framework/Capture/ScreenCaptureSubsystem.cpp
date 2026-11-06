// SPDX-License-Identifier: MIT
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Capture/ScreenCaptureSubsystem.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Capture/Encoding/CaptureFileWriter.h"
#include "Library/Framework/Capture/Encoding/CaptureImage.h"
#include "Library/Framework/Capture/AlphaMatte.h"
#include "Library/Framework/Capture/Encoding/GifEncoder.h"
#include "Library/Framework/Capture/OffscreenShot.h"
#include "Library/Framework/Capture/ScreenshotNames.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/GameInstance/GameInstance.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogCapture, Log )

REGISTER_SUBSYSTEM( CScreenCaptureSubsystem )

namespace
{
	FORCEINLINE EFrameCaptureStage ToStage( bool bIncludeDebugUI )
	{
		return bIncludeDebugUI ? EFrameCaptureStage::WithDebugUI : EFrameCaptureStage::Game;
	}
}

CScreenCaptureSubsystem::CScreenCaptureSubsystem() = default;

CScreenCaptureSubsystem::~CScreenCaptureSubsystem() = default;

void CScreenCaptureSubsystem::Initialize( FSubsystemCollection& Collection )
{
	CAppSubsystem::Initialize( Collection );
	Directory = NScreenshotNames::GetDefaultDirectory();
	CommandLine = NCaptureCommandLine::Parse( NCommandLine::Get() );
	Platform = GetAppSubsystem<IPlatform>();
	if ( Platform != nullptr )
	{
		Platform->SetFrameCaptureSink( this );
	}
}

void CScreenCaptureSubsystem::Deinitialize()
{
	if ( bRecording )
	{
		StopRecording();
	}
	if ( Platform != nullptr )
	{
		Platform->SetFrameCaptureSink( nullptr );
		Platform = nullptr;
	}
	CAppSubsystem::Deinitialize();
}

void CScreenCaptureSubsystem::SetDirectory( const FString& InDirectory )
{
	GAME_THREAD_ONLY();
	Directory = InDirectory.IsEmpty() ? NScreenshotNames::GetDefaultDirectory() : InDirectory;
}

void CScreenCaptureSubsystem::Tick( float32 DeltaTime )
{
	( void )DeltaTime;
	if ( Platform == nullptr )
	{
		return;
	}
	// いま描いているフレームの番号（EndFrame で、この番号の画素が届く）
	const uint64 FrameIndex = Platform->GetFrameCount() + 1;
	ApplyCommandLine( FrameIndex );
	TakePendingHighResShot();

	const IInput* Input = GetAppSubsystem<IInput>();
	if ( bHotkeysEnabled && Input != nullptr && Input->IsKeyPressed( EKey::F9 ) )
	{
		const bool bShift = Input->IsKeyDown( EKey::LeftShift ) || Input->IsKeyDown( EKey::RightShift );
		if ( !bShift )
		{
			TakeScreenshot( FString(), HotkeyScreenshotOptions );
		}
		else if ( bRecording )
		{
			StopRecording();
		}
		else
		{
			StartRecording( FString(), HotkeyRecordOptions );
		}
	}
}

void CScreenCaptureSubsystem::ApplyCommandLine( uint64 FrameIndex )
{
	if ( CommandLine.bScreenshot && !bCommandScreenshotIssued && FrameIndex >= static_cast<uint64>( NMath::Max( 1, CommandLine.ScreenshotFrame ) ) )
	{
		bCommandScreenshotIssued = true;
		if ( CommandLine.bHighRes )
		{
			if ( !CommandLine.SubjectName.IsEmpty() )
			{
				SetSubjectByName( CommandLine.SubjectName );
			}
			TakeHighResShot( CommandLine.ScreenshotName, CommandLine.HighRes );
		}
		else
		{
			TakeScreenshot( CommandLine.ScreenshotName, CommandLine.Screenshot );
		}
	}
	if ( CommandLine.bRecord && !bCommandRecordIssued && FrameIndex >= static_cast<uint64>( NMath::Max( 1, CommandLine.RecordStartFrame ) ) )
	{
		bCommandRecordIssued = true;
		StartRecording( CommandLine.RecordName, CommandLine.Record );
	}
	// 起動引数で頼まれたものが、すべて終わったら閉じる
	const bool bWantsExit = ( CommandLine.bScreenshot && CommandLine.bExitAfterScreenshot ) || ( CommandLine.bRecord && CommandLine.bExitAfterRecord );
	const bool bScreenshotDone = !CommandLine.bScreenshot || ( bCommandScreenshotIssued && !bScreenshotPending && !bHighResPending );
	const bool bRecordDone = !CommandLine.bRecord || ( bCommandRecordIssued && !bRecording );
	if ( bWantsExit && bScreenshotDone && bRecordDone && Platform != nullptr && !Platform->IsExitRequested() )
	{
		Platform->RequestExit();
	}
}

FString CScreenCaptureSubsystem::TakeScreenshot( const FString& Name, const FScreenshotOptions& Options )
{
	GAME_THREAD_ONLY( FString() );
	if ( Platform == nullptr || bScreenshotPending )
	{
		return FString();
	}
	const EScreenshotFormat Format = NCaptureFileWriter::ResolveFormat( Name, Options.Format );
	ScreenshotOptions = Options;
	ScreenshotOptions.Format = Format;
	ScreenshotPath = NScreenshotNames::ResolvePath( Directory, Name, NCaptureFileWriter::GetExtension( Format ), LastNumber + 1, &LastNumber );
	ScreenshotFrame = Platform->GetFrameCount() + 1 + static_cast<uint64>( NMath::Max( 0, Options.DelayFrames ) );
	bScreenshotPending = true;
	return ScreenshotPath;
}

FString CScreenCaptureSubsystem::TakeHighResShot( const FString& Name, const FHighResShotOptions& Options )
{
	GAME_THREAD_ONLY( FString() );
	int32 Width = 0;
	int32 Height = 0;
	float32 Scale = 1.0f;
	if ( Platform == nullptr || bHighResPending || ( Options.bSubjectOnly && !Subject ) || !NOffscreenShot::ResolveSize( Options, Platform->GetScreenWidth(), Platform->GetScreenHeight(), Width, Height, Scale ) )
	{
		return FString();
	}
	HighResOptions = Options;
	HighResOptions.Format = NCaptureFileWriter::ResolveFormat( Name, Options.Format );
	HighResPath = NScreenshotNames::ResolvePath( Directory, Name, NCaptureFileWriter::GetExtension( HighResOptions.Format ), LastNumber + 1, &LastNumber );
	bHighResPending = true;
	return HighResPath;
}

void CScreenCaptureSubsystem::SetSubject( const TCallable<bool( const CGameObject& )>& Filter )
{
	GAME_THREAD_ONLY();
	Subject = Filter;
}

void CScreenCaptureSubsystem::SetSubjectByName( const FString& Text )
{
	GAME_THREAD_ONLY();
	if ( Text.IsEmpty() )
	{
		ClearSubject();
		return;
	}
	Subject = [ Text ]( const CGameObject& Object )
	{
		return Object.GetName().Contains( Text, ESearchCase::IgnoreCase );
	};
}

void CScreenCaptureSubsystem::ClearSubject()
{
	GAME_THREAD_ONLY();
	Subject = nullptr;
}

void CScreenCaptureSubsystem::TakePendingHighResShot()
{
	if ( !bHighResPending )
	{
		return;
	}
	bHighResPending = false;
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	CGameInstance* GameInstance = GetEngine() != nullptr ? GetEngine()->GetGameInstance() : nullptr;
	int32 Width = 0;
	int32 Height = 0;
	float32 Scale = 1.0f;
	TArray<FColor32> Pixels;
	const bool bSized = Platform != nullptr && NOffscreenShot::ResolveSize( HighResOptions, Platform->GetScreenWidth(), Platform->GetScreenHeight(), Width, Height, Scale );
	const TCallable<bool( const CGameObject& )>* Filter = HighResOptions.bSubjectOnly && Subject ? &Subject : nullptr;
	const bool bRendered = bSized && Executor != nullptr && GameInstance != nullptr && NOffscreenShot::Render( *Executor, GameInstance->GetSceneManager(), Width, Height, Scale, Filter, Pixels );
	if ( bRendered && Filter != nullptr && HighResOptions.Format != EScreenshotFormat::Png )
	{
		// 透明度を残せない形式は、背景の色の上に重ねる
		NAlphaMatte::Flatten( Pixels, HighResOptions.BackgroundColor );
	}
	bLastSaveSucceeded = bRendered && NCaptureFileWriter::Save( Pixels, Width, Height, HighResPath, HighResOptions.Format, HighResOptions.JpegQuality, GetAppSubsystem<IAssetBackend>() );
	LastSavedPath = HighResPath;
	if ( bLastSaveSucceeded )
	{
		LOG( LogCapture, Log, "Saved a high resolution screenshot (%d x %d%s) to %s", Width, Height, Filter != nullptr ? ", subject only" : "", *HighResPath );
	}
	else
	{
		LOG( LogCapture, Warning, "Failed to take the high resolution screenshot %s (%d x %d)", *HighResPath, Width, Height );
	}
}

FString CScreenCaptureSubsystem::StartRecording( const FString& Name, const FGifRecordOptions& Options )
{
	GAME_THREAD_ONLY( FString() );
	if ( Platform == nullptr || bRecording )
	{
		return FString();
	}
	RecordOptions = Options;
	RecordOptions.FrameStep = NMath::Max( 1, Options.FrameStep );
	RecordOptions.AppFramesPerSecond = NMath::Max( 1, Options.AppFramesPerSecond );
	RecordPath = NScreenshotNames::ResolvePath( Directory, Name, ".gif", LastNumber + 1, &LastNumber );
	RecordStartFrame = Platform->GetFrameCount() + 1;
	RecordedFrames = 0;
	DelayCarry = 0.0;
	Encoder.Reset();
	bRecording = true;
	LOG( LogCapture, Log, "Recording a GIF to %s", *RecordPath );
	return RecordPath;
}

bool CScreenCaptureSubsystem::StopRecording()
{
	GAME_THREAD_ONLY( false );
	if ( !bRecording )
	{
		return false;
	}
	bRecording = false;
	const TArray<uint8> Bytes = Encoder.IsValid() ? Encoder->Finish() : TArray<uint8>();
	Encoder.Reset();
	const FString Normalized = RecordPath.Replace( "\\", "/" );
	const int32 Slash = Normalized.FindLast( "/" );
	const bool bFolder = Slash <= 0 || NFileSystem::EnsureDirectory( Normalized.Left( Slash ) );
	bLastSaveSucceeded = Bytes.Num() > 0 && bFolder && NFile::WriteAllBytes( RecordPath, Bytes.GetData(), static_cast<usize>( Bytes.Num() ) );
	LastSavedPath = RecordPath;
	if ( bLastSaveSucceeded )
	{
		LOG( LogCapture, Log, "Saved a GIF (%d frame(s), %d byte(s)) to %s", RecordedFrames, Bytes.Num(), *RecordPath );
	}
	else
	{
		LOG( LogCapture, Warning, "Failed to save the GIF %s (%d frame(s))", *RecordPath, RecordedFrames );
	}
	return bLastSaveSucceeded;
}

bool CScreenCaptureSubsystem::WantsFrame( uint64 FrameIndex, EFrameCaptureStage Stage ) const
{
	const bool bScreenshot = bScreenshotPending && FrameIndex >= ScreenshotFrame && Stage == ToStage( ScreenshotOptions.bIncludeDebugUI );
	const bool bRecord = bRecording && FrameIndex >= RecordStartFrame && ( FrameIndex - RecordStartFrame ) % static_cast<uint64>( RecordOptions.FrameStep ) == 0 && Stage == ToStage( RecordOptions.bIncludeDebugUI );
	return bScreenshot || bRecord;
}

void CScreenCaptureSubsystem::OnFrame( const FFramePixels& Frame )
{
	if ( Frame.Pixels == nullptr )
	{
		return;
	}
	if ( bScreenshotPending && Frame.FrameIndex >= ScreenshotFrame && Frame.Stage == ToStage( ScreenshotOptions.bIncludeDebugUI ) )
	{
		SaveScreenshot( Frame );
	}
	if ( bRecording && WantsFrame( Frame.FrameIndex, Frame.Stage ) )
	{
		RecordFrame( Frame );
	}
}

void CScreenCaptureSubsystem::SaveScreenshot( const FFramePixels& Frame )
{
	bScreenshotPending = false;
	int32 Width = 0;
	int32 Height = 0;
	const TArray<FColor32> Pixels = NCaptureImage::Extract( TArrayView<const FColor32>( Frame.Pixels, Frame.Width * Frame.Height ), Frame.Width, Frame.Height, ScreenshotOptions.Region, ScreenshotOptions.Downscale, Width, Height );
	bLastSaveSucceeded = NCaptureFileWriter::Save( Pixels, Width, Height, ScreenshotPath, ScreenshotOptions.Format, ScreenshotOptions.JpegQuality, GetAppSubsystem<IAssetBackend>() );
	LastSavedPath = ScreenshotPath;
	if ( bLastSaveSucceeded )
	{
		LOG( LogCapture, Log, "Saved a screenshot (%d x %d) of frame %llu to %s", Width, Height, static_cast<unsigned long long>( Frame.FrameIndex ), *ScreenshotPath );
	}
	else
	{
		LOG( LogCapture, Warning, "Failed to save the screenshot %s", *ScreenshotPath );
	}
}

void CScreenCaptureSubsystem::RecordFrame( const FFramePixels& Frame )
{
	int32 Width = 0;
	int32 Height = 0;
	const TArray<FColor32> Pixels = NCaptureImage::Extract( TArrayView<const FColor32>( Frame.Pixels, Frame.Width * Frame.Height ), Frame.Width, Frame.Height, RecordOptions.Region, RecordOptions.Downscale, Width, Height );
	if ( Pixels.Num() == 0 )
	{
		return;
	}
	if ( !Encoder.IsValid() )
	{
		Encoder = MakeUnique<FGifEncoder>( Width, Height, RecordOptions.bLoop );
	}
	// 1 枚の表示時間（1/100 秒）。端数は次の 1 枚へ持ち越して、全体の長さを合わせる
	const float64 Exact = 100.0 * static_cast<float64>( RecordOptions.FrameStep ) / static_cast<float64>( RecordOptions.AppFramesPerSecond ) + DelayCarry;
	const int32 Delay = NMath::Max( 1, static_cast<int32>( Exact + 0.5 ) );
	DelayCarry = Exact - static_cast<float64>( Delay );
	Encoder->AddFrame( Pixels, Delay );
	++RecordedFrames;
	if ( RecordOptions.MaxFrames > 0 && RecordedFrames >= RecordOptions.MaxFrames )
	{
		StopRecording();
	}
}
