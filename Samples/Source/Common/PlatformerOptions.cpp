#include "PlatformerOptions.h"

#include "Library/Platform/System/CommandLine.h"

FPlatformerOptions FPlatformerOptions::FromCommandLine()
{
	FPlatformerOptions Options;
	Options.bAutoPlay = NCommandLine::HasFlag( "autoplay" );
	Options.bStar = NCommandLine::HasFlag( "star" );
	const TOptional<FString> Form = NCommandLine::GetValue( "form" );
	if ( Form.IsSet() && Form.GetValue() == "big" )
	{
		Options.Form = EPlatformerForm::Big;
	}
	else if ( Form.IsSet() && Form.GetValue() == "fire" )
	{
		Options.Form = EPlatformerForm::Fire;
	}
	return Options;
}

void FPlatformerOptions::ApplyTo( FPlatformerStatus& Status ) const
{
	Status.Form = Form;
	if ( bStar )
	{
		Status.StarTime = FPlatformerStatus::StarSeconds;
	}
}
