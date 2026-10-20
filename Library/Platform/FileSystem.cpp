#include "FileSystem.h"

#include "Library/Core/Log/Log.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogFileSystem, Log )

namespace
{
	bool IsAbsolutePath( const FString& Path )
	{
		// "C:..." / "\\server\..." / "/..."
		return ( Path.Len() >= 2 && Path[1] == ':' ) || Path.StartsWith( "\\" ) || Path.StartsWith( "/" );
	}

	// 最後の区切りより前（区切りが無ければ空）
	FString GetParentDirectory( const FString& Directory )
	{
		const int32 Backslash = Directory.FindLast( "\\" );
		const int32 Slash = Directory.FindLast( "/" );
		const int32 Index = Backslash > Slash ? Backslash : Slash;
		return Index > 0 ? Directory.Left( Index ) : FString();
	}

	FString ToWindowsPath( const FString& Path )
	{
		return Path.Replace( "/", "\\" );
	}

	FFileTime ToFileTime( const SYSTEMTIME& Time )
	{
		FFileTime Result;
		Result.Year = Time.wYear;
		Result.Month = Time.wMonth;
		Result.Day = Time.wDay;
		Result.Hour = Time.wHour;
		Result.Minute = Time.wMinute;
		Result.Second = Time.wSecond;
		return Result;
	}

	// 作業ディレクトリ -> exe のフォルダ -> その親 の順に Exists( パス ) を試して、最初に見つかったものを返す
	template<typename Predicate>
	TOptional<FString> FindFromBaseDirectories( const char* Kind, const FString& RelativePath, int32 MaxParentLevels, Predicate Exists )
	{
		// 見つからないのが普通のこともある（省略可能な設定ファイルなど）ので、結果は Verbose。呼び出し側が重大度を決める
		if ( IsAbsolutePath( RelativePath ) )
		{
			if ( Exists( RelativePath ) ) return RelativePath;
			LOG( LogFileSystem, Verbose, "%s not found: %s", Kind, *RelativePath );
			return NullOpt;
		}

		// 作業ディレクトリ
		if ( Exists( RelativePath ) )
		{
			LOG( LogFileSystem, Verbose, "Found %s %s (working directory)", Kind, *RelativePath );
			return RelativePath;
		}

		// exe のフォルダとその親
		FString Directory = NFileSystem::GetExecutableDirectory();
		for ( int32 Level = 0; Level <= MaxParentLevels && !Directory.IsEmpty(); ++Level )
		{
			const FString Candidate = Directory / RelativePath;
			if ( Exists( Candidate ) )
			{
				LOG( LogFileSystem, Verbose, "Found %s %s", Kind, *Candidate );
				return Candidate;
			}
			Directory = GetParentDirectory( Directory );
		}
		LOG( LogFileSystem, Verbose, "%s not found: %s (searched the working directory and up to %d level(s) above the executable)", Kind, *RelativePath, MaxParentLevels );
		return NullOpt;
	}
}

FString FFileTime::ToFileNameString() const
{
	return FString::Printf( "%04d-%02d-%02d_%02d-%02d-%02d", Year, Month, Day, Hour, Minute, Second );
}

#pragma region Exists

bool NFileSystem::FileExists( const FString& Path )
{
	const DWORD Attributes = GetFileAttributesA( *Path );
	return Attributes != INVALID_FILE_ATTRIBUTES && ( Attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
}

bool NFileSystem::DirectoryExists( const FString& Path )
{
	const DWORD Attributes = GetFileAttributesA( *Path );
	return Attributes != INVALID_FILE_ATTRIBUTES && ( Attributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
}

#pragma endregion

#pragma region Locations

FString NFileSystem::GetExecutableDirectory()
{
	char Buffer[MAX_PATH] = {};
	const DWORD Length = GetModuleFileNameA( nullptr, Buffer, MAX_PATH );
	if ( Length == 0 || Length >= MAX_PATH ) return FString();
	return GetParentDirectory( FString( Buffer ) );
}

FString NFileSystem::GetExecutableName()
{
	char Buffer[MAX_PATH] = {};
	const DWORD Length = GetModuleFileNameA( nullptr, Buffer, MAX_PATH );
	if ( Length == 0 || Length >= MAX_PATH ) return FString( "game" );

	const FString FullPath( Buffer );
	const int32 Backslash = FullPath.FindLast( "\\" );
	const int32 Slash = FullPath.FindLast( "/" );
	const int32 SeparatorIndex = Backslash > Slash ? Backslash : Slash;
	const FString FileName = SeparatorIndex == INDEX_NONE ? FullPath : FullPath.RightChop( SeparatorIndex + 1 );

	const int32 DotIndex = FileName.FindLast( "." );
	return DotIndex > 0 ? FileName.Left( DotIndex ) : FileName;
}

FString NFileSystem::GetWorkingDirectory()
{
	char Buffer[MAX_PATH] = {};
	const DWORD Length = GetCurrentDirectoryA( MAX_PATH, Buffer );
	if ( Length == 0 || Length >= MAX_PATH ) return FString();
	return FString( Buffer );
}

FString NFileSystem::GetFullPath( const FString& Path )
{
	char Buffer[MAX_PATH] = {};
	const DWORD Length = GetFullPathNameA( *Path, MAX_PATH, Buffer, nullptr );
	if ( Length == 0 || Length >= MAX_PATH ) return Path;
	return FString( Buffer );
}

TOptional<FString> NFileSystem::FindFile( const FString& RelativePath, int32 MaxParentLevels )
{
	return FindFromBaseDirectories( "file", RelativePath, MaxParentLevels, []( const FString& Path ) { return FileExists( Path ); } );
}

TOptional<FString> NFileSystem::FindDirectory( const FString& RelativePath, int32 MaxParentLevels )
{
	return FindFromBaseDirectories( "directory", RelativePath, MaxParentLevels, []( const FString& Path ) { return DirectoryExists( Path ); } );
}

FString NFileSystem::FindProjectRoot()
{
	const TOptional<FString> ConfigDirectory = FindDirectory( "Config" );
	if ( !ConfigDirectory.IsSet() )
	{
		LOG( LogFileSystem, Warning, "Config folder not found. Using the working directory as the project root: %s", *GetWorkingDirectory() );
		return GetWorkingDirectory();
	}

	const FString FullPath = GetFullPath( ConfigDirectory.GetValue() );
	const FString Parent = GetParentDirectory( FullPath );
	return Parent.IsEmpty() ? GetWorkingDirectory() : Parent;
}

#pragma endregion

#pragma region Operations

bool NFileSystem::EnsureDirectory( const FString& Path )
{
	if ( Path.IsEmpty() ) return false;

	// "A/B/C" なら A、A/B、A/B/C の順に作る
	const FString Normalized = ToWindowsPath( Path );
	for ( int32 i = 1; i <= Normalized.Len(); ++i )
	{
		if ( i != Normalized.Len() && Normalized[i] != '\\' ) continue;

		const FString Partial = Normalized.Left( i );
		// "C:" のようなドライブ名は作らない
		if ( Partial.Len() == 2 && Partial[1] == ':' ) continue;
		if ( CreateDirectoryA( *Partial, nullptr ) )
		{
			LOG( LogFileSystem, Verbose, "Created directory: %s", *Partial );
		}
		else if ( GetLastError() != ERROR_ALREADY_EXISTS )
		{
			LOG( LogFileSystem, Warning, "Failed to create directory %s (Windows error %lu)", *Partial, GetLastError() );
			return false;
		}
	}
	return true;
}

TArray<FString> NFileSystem::ListFiles( const FString& Directory, const FString& Pattern )
{
	TArray<FString> Result;

	WIN32_FIND_DATAA Data;
	HANDLE Handle = FindFirstFileA( *( ToWindowsPath( Directory ) / Pattern ), &Data );
	if ( Handle == INVALID_HANDLE_VALUE ) return Result;

	do
	{
		if ( ( Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 ) Result.Add( FString( Data.cFileName ) );
	}
	while ( FindNextFileA( Handle, &Data ) );
	FindClose( Handle );
	return Result;
}

namespace
{
	void ListRecursiveInto( const FString& Root, const FString& Relative, const TArray<FString>& Extensions, TArray<FString>& Out, int32 Depth )
	{
		if ( Depth > 16 ) return;   // シンボリックリンクなどでの無限の再帰を避ける

		WIN32_FIND_DATAA Data;
		const FString Directory = Relative.IsEmpty() ? Root : ToWindowsPath( Root ) / ToWindowsPath( Relative );
		HANDLE Handle = FindFirstFileA( *( Directory / "*" ), &Data );
		if ( Handle == INVALID_HANDLE_VALUE ) return;

		do
		{
			const FString Name( Data.cFileName );
			if ( Name == "." || Name == ".." ) continue;
			const FString Child = Relative.IsEmpty() ? Name : Relative + "/" + Name;
			if ( ( Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
			{
				ListRecursiveInto( Root, Child, Extensions, Out, Depth + 1 );
				continue;
			}
			bool bMatch = Extensions.IsEmpty();
			for ( const FString& Extension : Extensions )
			{
				if ( Name.EndsWith( Extension, ESearchCase::IgnoreCase ) ) bMatch = true;
			}
			if ( bMatch ) Out.Add( Child );
		}
		while ( FindNextFileA( Handle, &Data ) );
		FindClose( Handle );
	}
}

TArray<FString> NFileSystem::ListFilesRecursive( const FString& Directory, const TArray<FString>& Extensions )
{
	TArray<FString> Result;
	ListRecursiveInto( Directory, FString(), Extensions, Result, 0 );
	Result.Sort( []( const FString& A, const FString& B ) { return A.Compare( B, ESearchCase::IgnoreCase ) < 0; } );
	return Result;
}

int64 NFileSystem::GetFileSize( const FString& Path )
{
	WIN32_FILE_ATTRIBUTE_DATA Data;
	if ( !GetFileAttributesExA( *Path, GetFileExInfoStandard, &Data ) || ( Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) return -1;
	return ( static_cast<int64>( Data.nFileSizeHigh ) << 32 ) | static_cast<int64>( Data.nFileSizeLow );
}

bool NFileSystem::GetLastWriteTime( const FString& Path, FFileTime& OutTime )
{
	WIN32_FILE_ATTRIBUTE_DATA Data;
	if ( !GetFileAttributesExA( *Path, GetFileExInfoStandard, &Data ) ) return false;

	FILETIME Local;
	SYSTEMTIME System;
	if ( !FileTimeToLocalFileTime( &Data.ftLastWriteTime, &Local ) || !FileTimeToSystemTime( &Local, &System ) ) return false;

	OutTime = ToFileTime( System );
	return true;
}

FFileTime NFileSystem::GetCurrentLocalTime()
{
	SYSTEMTIME Time;
	GetLocalTime( &Time );
	return ToFileTime( Time );
}

bool NFileSystem::RenameFile( const FString& From, const FString& To )
{
	if ( MoveFileExA( *From, *To, 0 ) == 0 )
	{
		LOG( LogFileSystem, Warning, "Failed to rename %s -> %s (Windows error %lu)", *From, *To, GetLastError() );
		return false;
	}
	LOG( LogFileSystem, Verbose, "Renamed %s -> %s", *From, *To );
	return true;
}

bool NFileSystem::RemoveFile( const FString& Path )
{
	if ( DeleteFileA( *Path ) == 0 )
	{
		LOG( LogFileSystem, Warning, "Failed to delete %s (Windows error %lu)", *Path, GetLastError() );
		return false;
	}
	LOG( LogFileSystem, Verbose, "Deleted %s", *Path );
	return true;
}

#pragma endregion
