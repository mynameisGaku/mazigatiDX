#include "LogArchive.h"

#include "Library/Platform/FileSystem.h"

namespace
{
	// "<Name>_Backup_<日時>.log" の日時より前の部分
	FString MakeBackupPrefix( const FString& Name )
	{
		return Name + "_Backup_";
	}

	// 改名先。同じ名前があれば "_1", "_2" ... を付けて重ならないようにする
	FString MakeUniqueBackupPath( const FString& Directory, const FString& Name, const FString& Stamp )
	{
		const FString Base = Directory / ( MakeBackupPrefix( Name ) + Stamp );
		FString Path = Base + ".log";
		for ( int32 Suffix = 1; NFileSystem::FileExists( Path ); ++Suffix )
		{
			Path = Base + FString::Printf( "_%d.log", Suffix );
		}
		return Path;
	}
}

NLogArchive::FPrepareResult NLogArchive::Prepare( const FString& Directory, const FString& Name, int32 MaxBackups )
{
	FPrepareResult Result;
	Result.bDirectoryReady = NFileSystem::EnsureDirectory( Directory );
	if ( !Result.bDirectoryReady ) return Result;

	// 1. 今回のログと同じ名前のファイルが既にあれば、古いログとして改名する
	const FString CurrentPath = Directory / ( Name + ".log" );
	if ( NFileSystem::FileExists( CurrentPath ) )
	{
		FFileTime Time;
		if ( !NFileSystem::GetLastWriteTime( CurrentPath, Time ) ) Time = NFileSystem::GetCurrentLocalTime();

		const FString BackupPath = MakeUniqueBackupPath( Directory, Name, Time.ToFileNameString() );
		if ( NFileSystem::RenameFile( CurrentPath, BackupPath ) ) Result.BackupPath = BackupPath;
		else Result.bBackupFailed = true;
	}

	// 2. 古いバックアップを整理する。名前に日時が入っているので、文字列順 = 古い順
	TArray<FString> Backups = NFileSystem::ListFiles( Directory, MakeBackupPrefix( Name ) + "*.log" );
	Backups.Sort();

	const int32 Limit = MaxBackups < 0 ? 0 : MaxBackups;
	int32 NumToDelete = Backups.Num() - Limit;
	for ( int32 i = 0; i < Backups.Num() && NumToDelete > 0; ++i )
	{
		if ( NFileSystem::RemoveFile( Directory / Backups[i] ) )
		{
			++Result.NumDeleted;
			--NumToDelete;
		}
	}

	Result.NumBackups = Backups.Num() - Result.NumDeleted;
	return Result;
}

NLogArchive::FOpenResult NLogArchive::OpenLogFile( const FString& Directory, const FString& Name, int32 MaxBackups, int32 MaxInstances )
{
	FOpenResult Result;

	for ( int32 Index = 1; Index <= MaxInstances; ++Index )
	{
		const FString InstanceName = Index == 1 ? Name : Name + FString::Printf( "_%d", Index );

		// 使用中の <Name>.log は改名できない（Prepare は bBackupFailed になるだけで、他は問題なく進む）
		const FPrepareResult Archive = Prepare( Directory, InstanceName, MaxBackups );
		if ( !Archive.bDirectoryReady ) return Result;

		const FString Path = Directory / ( InstanceName + ".log" );
		TSharedPtr<CFileLogSink> Sink = MakeShared<CFileLogSink>( Path, false, true );
		if ( !Sink->IsOpen() ) continue;   // 別のインスタンスが使用中 -> 次の名前

		Result.Sink = Sink;
		Result.Path = Path;
		Result.InstanceIndex = Index;
		Result.Archive = Archive;
		return Result;
	}
	return Result;
}
