#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FileSystem
//   ファイル・ディレクトリの操作とパス解決（Windows API）
//
//   // 作業ディレクトリ -> exe のフォルダ -> その親 ... の順に探す
//   //   VS からのデバッグ実行（作業ディレクトリ = プロジェクト）
//   //   x64/Debug の exe を直接起動（2 つ上がプロジェクト）
//   //   配布時（exe の隣に Config）
//   // のどれでも見つかる
//   if ( TOptional<FString> Path = NFileSystem::FindFile( "Config/ScreenConfig.json" ); Path.IsSet() ) ...
//
//   区切りは '/' でも '\' でもよい。戻り値のパスは渡した形式のまま（FindFile / FindDirectory は見つけた場所を付けて返す）
//=============================================================================

// 日時（ローカルタイム）
struct FFileTime
{
	int32 Year = 0;
	int32 Month = 0;
	int32 Day = 0;
	int32 Hour = 0;
	int32 Minute = 0;
	int32 Second = 0;

	// "2026-10-16_23-19-14"（ファイル名に使える形式。文字列で並べると時系列になる）
	FString ToFileNameString() const;
};

namespace NFileSystem
{
	//-------------------------------------------------------------------------
	// 存在確認
	//-------------------------------------------------------------------------
	bool FileExists( const FString& Path );
	bool DirectoryExists( const FString& Path );

	//-------------------------------------------------------------------------
	// 場所
	//-------------------------------------------------------------------------
	// exe があるフォルダ（末尾の区切りなし）
	FString GetExecutableDirectory();
	// exe のファイル名から拡張子を除いたもの（VS のプロジェクト名と同じ。例: "project"）
	FString GetExecutableName();
	// 作業ディレクトリ（絶対パス）
	FString GetWorkingDirectory();
	// 絶対パスにする。失敗したらそのまま返す
	FString GetFullPath( const FString& Path );

	// 絶対パスならそのまま確認する。相対パスなら作業ディレクトリ、exe のフォルダ、
	// exe のフォルダから MaxParentLevels 階層上までを順に探し、最初に見つかったパスを返す
	TOptional<FString> FindFile( const FString& RelativePath, int32 MaxParentLevels = 3 );
	TOptional<FString> FindDirectory( const FString& RelativePath, int32 MaxParentLevels = 3 );

	// プロジェクトのルート（Config フォルダがあるフォルダ。Saved などの保存先の基準）。見つからなければ作業ディレクトリ
	FString FindProjectRoot();

	//-------------------------------------------------------------------------
	// 操作
	//-------------------------------------------------------------------------
	// ディレクトリを作る（途中の階層も作る）。既にあるか、作れたら true
	bool EnsureDirectory( const FString& Path );

	// Directory の中のファイル名（パスなし）を、ワイルドカード（例: "project_Backup_*.log"）で列挙する。ディレクトリは含まない
	TArray<FString> ListFiles( const FString& Directory, const FString& Pattern );
	// Directory の下（サブフォルダも）にあるファイルを、拡張子（".png" のようにドットつき、大文字小文字は区別しない。
	// 複数なら Extensions に並べる。空なら全部）で列挙する。戻り値は Directory からの相対パス（区切りは '/'）。ディレクトリは含まない
	TArray<FString> ListFilesRecursive( const FString& Directory, const TArray<FString>& Extensions );
	// ファイルの大きさ（バイト）。無ければ -1
	int64 GetFileSize( const FString& Path );

	bool GetLastWriteTime( const FString& Path, FFileTime& OutTime );
	FFileTime GetCurrentLocalTime();

	// 移動・改名。移動先が既にあれば失敗する
	bool RenameFile( const FString& From, const FString& To );
	bool RemoveFile( const FString& Path );
}
