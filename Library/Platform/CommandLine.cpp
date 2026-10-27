#include "CommandLine.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

FString NCommandLine::Get()
{
	const char* CommandLine = GetCommandLineA();
	return CommandLine != nullptr ? FString( CommandLine ) : FString();
}
