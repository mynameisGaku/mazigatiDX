#include "AssetManager.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"
#include "Library/Platform/FileSystem.h"

//=============================================================================
// CAssetManager: モデルの取り込みと .mv1 への出力
//=============================================================================
bool CAssetManager::ResolveModelRequest( const FString& Path, FString& OutFullPath ) const
{
	if ( GetBackend() == nullptr )
	{
		LOG( LogAsset, Error, "Cannot load the model '%s': no asset backend.", *Path );
		return false;
	}

	TOptional<FString> Found = ResolvePath( Path );
	// 元の .fbx が無くて、変換済みの .mv1 だけがある（配布版など）
	if ( !Found.IsSet() && NModelFormat::CanConvertToMV1( Path ) ) Found = ResolvePath( NModelFormat::GetMV1Path( Path ) );
	if ( !Found.IsSet() )
	{
		LOG( LogAsset, Error, "Cannot load the model '%s': the file was not found (root directory '%s').", *Path, *GetRootDirectory() );
		return false;
	}
	OutFullPath = Found.Get( FString() );
	return true;
}

bool CAssetManager::ResolveModelLoadPath( const FString& FullPath, FString& OutLoadPath )
{
	OutLoadPath = FullPath;
	const EModelFileKind Kind = NModelFormat::Classify( FullPath );
	if ( Kind != EModelFileKind::Imported && !( Kind == EModelFileKind::DxLibNative && bPreferConvertedModels ) ) return true;

	// 最新の .mv1 が隣にあれば、それを読む
	const FString Mv1Path = NModelFormat::GetMV1Path( FullPath );
	const bool bHasMv1 = NFileSystem::FileExists( Mv1Path );
	FFileTime SourceTime, OutputTime;
	if ( bHasMv1 && NFileSystem::GetLastWriteTime( FullPath, SourceTime ) && NFileSystem::GetLastWriteTime( Mv1Path, OutputTime ) && NModelFormat::IsOutputUpToDate( SourceTime, OutputTime ) )
	{
		OutLoadPath = Mv1Path;
		return true;
	}

	// DxLib が読める形式は、そのまま読める
	if ( Kind != EModelFileKind::Imported ) return true;

	// DxLib は .fbx / .obj を読めない
	if ( bAutoImportModels )
	{
		const FModelExportResult Result = NModelExport::Export( *GetBackend(), FullPath );
		if ( Result.bSuccess )
		{
			OutLoadPath = Result.OutputPath;
			return true;
		}
		// 変換できなかった。DxLib の側で読めるかもしれないので（FBX SDK つきのライブラリなど）、元のファイルをそのまま読ませてみる。
		// それも読めなければ、LoadModel が失敗を報告する
		LOG( LogAsset, Warning, "The model '%s' could not be converted to .mv1 (%s). DxLib is asked to read it directly.", *FullPath, *Result.Error );
		return true;
	}
	if ( bHasMv1 )
	{
		LOG( LogAsset, Warning, "The model '%s' is newer than its .mv1, but automatic import is off. The old .mv1 is used.", *FullPath );
		OutLoadPath = Mv1Path;
		return true;
	}
	LOG( LogAsset, Error, "Cannot load the model '%s': DxLib cannot read this format, there is no .mv1 next to it, and automatic import is off (ExportModelAsMV1 makes it).", *FullPath );
	return false;
}

FModelExportResult CAssetManager::ExportModelAsMV1( const FString& SourcePath, const FModelExportOptions& Options )
{
	PROFILE_SCOPE_CAT( "Asset", "ExportModel" );
	FModelExportResult Result;
	Result.SourcePath = SourcePath;
	if ( GetBackend() == nullptr )
	{
		Result.Error = "no asset backend";
		LOG( LogAsset, Error, "Cannot export the model '%s' as .mv1: %s.", *SourcePath, *Result.Error );
		return Result;
	}

	const TOptional<FString> Full = ResolvePath( SourcePath );
	if ( !Full.IsSet() )
	{
		Result.Error = "the file was not found";
		LOG( LogAsset, Error, "Cannot export the model '%s' as .mv1: %s (root directory '%s').", *SourcePath, *Result.Error, *GetRootDirectory() );
		return Result;
	}

	FModelExportOptions Resolved = Options;
	if ( !Options.OutputPath.IsEmpty() )
	{
		const TOptional<FString> Output = PrepareSavePath( Options.OutputPath );
		if ( !Output.IsSet() )
		{
			Result.SourcePath = Full.Get( FString() );
			Result.Error = "the output folder could not be created";
			return Result;
		}
		Resolved.OutputPath = Output.Get( FString() );
	}
	return NModelExport::Export( *GetBackend(), Full.Get( FString() ), Resolved );
}

TArray<FModelSourceInfo> CAssetManager::ScanModelSources() const
{
	TArray<FModelSourceInfo> Infos;
	const FString Root = GetRootDirectory();
	if ( Root.IsEmpty() || !NFileSystem::DirectoryExists( Root ) ) return Infos;

	for ( const FString& Relative : NFileSystem::ListFilesRecursive( Root, NModelFormat::GetConvertibleExtensions() ) )
	{
		FModelSourceInfo Info;
		Info.Path = Relative;
		Info.OutputPath = NModelFormat::GetMV1Path( Relative );
		Info.Kind = NModelFormat::Classify( Relative );
		Info.SourceBytes = NFileSystem::GetFileSize( Root / Relative );

		const FString OutputFull = Root / Info.OutputPath;
		FFileTime SourceTime, OutputTime;
		if ( !NFileSystem::FileExists( OutputFull ) ) Info.Status = EModelExportStatus::Missing;
		else if ( NFileSystem::GetLastWriteTime( Root / Relative, SourceTime ) && NFileSystem::GetLastWriteTime( OutputFull, OutputTime ) && NModelFormat::IsOutputUpToDate( SourceTime, OutputTime ) )
			Info.Status = EModelExportStatus::UpToDate;
		else Info.Status = EModelExportStatus::Stale;
		if ( Info.Status != EModelExportStatus::Missing ) Info.OutputBytes = NMath::Max<int64>( NFileSystem::GetFileSize( OutputFull ), 0 );
		Infos.Add( Info );
	}
	return Infos;
}

FModelBatchExportResult CAssetManager::ExportAllModels( bool bForce, const FModelExportOptions& Options )
{
	FModelBatchExportResult Batch;
	FModelExportOptions Each = Options;
	Each.OutputPath.Empty();   // 1 つずつ、元の隣に出す
	Each.bForce = bForce;
	for ( const FModelSourceInfo& Info : ScanModelSources() )
	{
		if ( !bForce && Info.Status == EModelExportStatus::UpToDate ) continue;
		Batch.Add( ExportModelAsMV1( Info.Path, Each ) );
	}
	LOG( LogAsset, Log, "Exported the models as .mv1: %s", *Batch.GetSummary() );
	return Batch;
}

FModelBatchExportResult CAssetManager::ExportModelsUsedBy( const FModelAnimationDefinition& Definition, bool bForce )
{
	TArray<FString> Paths;
	const auto AddPath = [&Paths]( const FString& Path )
	{
		if ( !Path.IsEmpty() && NModelFormat::CanConvertToMV1( Path ) ) Paths.AddUnique( Path );
	};
	AddPath( Definition.ModelPath );
	AddPath( Definition.PreviewModelPath );
	for ( const FModelClipDef& Clip : Definition.Clips ) AddPath( Clip.SourcePath );

	FModelBatchExportResult Batch;
	FModelExportOptions Options;
	Options.bForce = bForce;
	for ( const FString& Path : Paths ) Batch.Add( ExportModelAsMV1( Path, Options ) );
	return Batch;
}
