// SPDX-License-Identifier: MIT
#include "ProjectPaths.h"

#include "Library/Platform/System/FileSystem.h"

FProjectPaths::FProjectPaths()
	: ProjectRoot( NFileSystem::FindProjectRoot() )
{
}

FProjectPaths::FProjectPaths( const FString& InProjectRoot )
	: ProjectRoot( InProjectRoot )
{
}

FString FProjectPaths::GetConfigDirectory() const
{
	return ProjectRoot / "Config";
}

FString FProjectPaths::GetSavedDirectory() const
{
	return ProjectRoot / "Saved";
}

FString FProjectPaths::GetLogsDirectory() const
{
	return GetSavedDirectory() / "Logs";
}

FString FProjectPaths::GetScreenshotsDirectory() const
{
	return GetSavedDirectory() / "Screenshots";
}

FString FProjectPaths::GetSaveGamesDirectory() const
{
	return GetSavedDirectory() / "SaveGames";
}

FString FProjectPaths::GetProfilerDirectory() const
{
	return GetSavedDirectory() / "Profiler";
}

FString FProjectPaths::GetConsoleHistoryFile() const
{
	return GetSavedDirectory() / "ConsoleHistory.txt";
}
