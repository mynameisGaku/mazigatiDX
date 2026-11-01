// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/CaptureCommandLine.h"

#include "Library/Platform/CommandLine.h"

namespace
{
	// "--Name" か "--Name=値" があるか。値があれば OutValue へ
	bool ReadSwitch( const FString& CommandLine, const char* Name, FString& OutValue )
	{
		if ( const TOptional<FString> Value = NCommandLine::GetValue( CommandLine, Name ) )
		{
			OutValue = Value.GetValue();
			return true;
		}
		return NCommandLine::HasFlag( CommandLine, Name );
	}

	void ReadInt( const FString& CommandLine, const char* Name, int32& InOut )
	{
		if ( const TOptional<FString> Value = NCommandLine::GetValue( CommandLine, Name ) )
		{
			InOut = Value.GetValue().ToInt( InOut );
		}
	}

	void ReadRegion( const FString& CommandLine, const char* Name, FCaptureRegion& InOut )
	{
		if ( const TOptional<FString> Value = NCommandLine::GetValue( CommandLine, Name ) )
		{
			NCaptureCommandLine::ParseRegion( Value.GetValue(), InOut );
		}
	}
}

bool NCaptureCommandLine::ParseRegion( const FString& Text, FCaptureRegion& Out )
{
	const TArray<FString> Parts = Text.ParseIntoArray( ",", false );
	if ( Parts.Num() != 4 )
	{
		return false;
	}
	int32 Values[4] = {};
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		const TOptional<int64> Value = Parts[Index].TrimStart().TrimEnd().TryParseInt();
		if ( !Value.IsSet() )
		{
			return false;
		}
		Values[Index] = static_cast<int32>( Value.GetValue() );
	}
	Out.X = Values[0];
	Out.Y = Values[1];
	Out.Width = Values[2];
	Out.Height = Values[3];
	return true;
}

bool NCaptureCommandLine::ParseFormat( const FString& Text, EScreenshotFormat& Out )
{
	const FString Name = Text.StartsWith( "." ) ? Text.Mid( 1 ) : Text;
	if ( Name.Equals( "png", ESearchCase::IgnoreCase ) )
	{
		Out = EScreenshotFormat::Png;
	}
	else if ( Name.Equals( "jpg", ESearchCase::IgnoreCase ) || Name.Equals( "jpeg", ESearchCase::IgnoreCase ) )
	{
		Out = EScreenshotFormat::Jpeg;
	}
	else if ( Name.Equals( "bmp", ESearchCase::IgnoreCase ) )
	{
		Out = EScreenshotFormat::Bmp;
	}
	else if ( Name.Equals( "gif", ESearchCase::IgnoreCase ) )
	{
		Out = EScreenshotFormat::Gif;
	}
	else
	{
		return false;
	}
	return true;
}

FCaptureCommandLine NCaptureCommandLine::Parse( const FString& CommandLine )
{
	FCaptureCommandLine Result;
	Result.bScreenshot = ReadSwitch( CommandLine, "capture", Result.ScreenshotName );
	ReadInt( CommandLine, "capture-frame", Result.ScreenshotFrame );
	ReadRegion( CommandLine, "capture-region", Result.Screenshot.Region );
	ReadInt( CommandLine, "capture-downscale", Result.Screenshot.Downscale );
	Result.Screenshot.bIncludeDebugUI = NCommandLine::HasFlag( CommandLine, "capture-ui" );
	Result.bExitAfterScreenshot = NCommandLine::HasFlag( CommandLine, "capture-exit" );
	if ( const TOptional<FString> Format = NCommandLine::GetValue( CommandLine, "capture-format" ) )
	{
		NCaptureCommandLine::ParseFormat( Format.GetValue(), Result.Screenshot.Format );
	}
	ReadInt( CommandLine, "capture-quality", Result.Screenshot.JpegQuality );

	Result.bRecord = ReadSwitch( CommandLine, "record", Result.RecordName );
	ReadInt( CommandLine, "record-start", Result.RecordStartFrame );
	Result.Record.MaxFrames = 60;
	ReadInt( CommandLine, "record-frames", Result.Record.MaxFrames );
	ReadInt( CommandLine, "record-step", Result.Record.FrameStep );
	ReadRegion( CommandLine, "record-region", Result.Record.Region );
	ReadInt( CommandLine, "record-downscale", Result.Record.Downscale );
	Result.Record.bIncludeDebugUI = NCommandLine::HasFlag( CommandLine, "record-ui" );
	Result.bExitAfterRecord = NCommandLine::HasFlag( CommandLine, "record-exit" );
	return Result;
}
