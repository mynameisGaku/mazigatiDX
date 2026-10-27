#include "ModelExporter.h"


#include "Library/Core/Collection/Array.h"
#include "Library/Core/File/File.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/Import/FbxImporter.h"
#include "Library/Framework/Asset/Import/ImportedScene.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"
#include "Library/Framework/Asset/Import/XFileWriter.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelExport, Log )

namespace
{
	FString NormalizeSlashes( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	FString GetDirectory( const FString& Path )
	{
		const FString Normalized = NormalizeSlashes( Path );
		const int32 Separator = Normalized.FindLast( "/" );
		return Separator == INDEX_NONE ? FString() : Normalized.Left( Separator );
	}

	FString GetFileName( const FString& Path )
	{
		const FString Normalized = NormalizeSlashes( Path );
		const int32 Separator = Normalized.FindLast( "/" );
		return Separator == INDEX_NONE ? Normalized : Normalized.Mid( Separator + 1 );
	}

	FString GetBaseName( const FString& Path )
	{
		const FString Name = GetFileName( Path );
		const int32 Dot = Name.FindLast( "." );
		return Dot == INDEX_NONE || Dot == 0 ? Name : Name.Left( Dot );
	}

	bool IsAbsolute( const FString& Path )
	{
		const FString P = NormalizeSlashes( Path );
		return P.StartsWith( "/" ) || ( P.Len() >= 2 && P[1] == ':' );
	}

	bool ReadFileBytes( const FString& Path, TArray<uint8>& OutBytes ) { return NFile::ReadAllBytes( Path, OutBytes ); }

	bool WriteFileBytes( const FString& Path, const void* Data, size_t Size ) { return NFile::WriteAllBytes( Path, Data, Size ); }

	void AddWarning( FModelExportResult& Result, const FString& Text )
	{
		if ( !Result.Warning.IsEmpty() ) Result.Warning += "\n";
		Result.Warning += Text;
		LOG( LogModelExport, Warning, "'%s': %s", *Result.SourcePath, *Text );
	}

	FModelExportResult Fail( FModelExportResult Result, const FString& Reason )
	{
		Result.bSuccess = false;
		Result.Error = Reason;
		LOG( LogModelExport, Error, "Cannot export '%s' as .mv1: %s", *Result.SourcePath, *Reason );
		return Result;
	}

	// 取り込んだシーンの画像を、.x（出力のフォルダにある）から見えるようにして、TextureFilename に書くパスを返す
	TArray<FString> PrepareTextures( const FImportedScene& Scene, const FString& SourceDirectory, const FString& OutputDirectory, const FString& OutputBase, FModelExportResult& Result )
	{
		TArray<FString> Paths;
		for ( const FImportedTexture& Texture : Scene.Textures )
		{
			if ( !Texture.EmbeddedData.IsEmpty() )
			{
				// 埋め込み: 出力の隣の <名前>.textures/ に取り出す
				const FString Folder = OutputBase + ".textures";
				NFileSystem::EnsureDirectory( OutputDirectory / Folder );
				const FString FileName = GetFileName( Texture.File );
				if ( !WriteFileBytes( OutputDirectory / Folder / FileName, Texture.EmbeddedData.GetData(), static_cast<size_t>( Texture.EmbeddedData.Num() ) ) )
				{
					AddWarning( Result, "could not write the embedded texture '" + FileName + "'" );
				}
				Paths.Add( Folder + "/" + FileName );
				continue;
			}

			// ファイル参照: 元のフォルダを基準に探す。無ければ、同じフォルダの同じファイル名
			FString Absolute = IsAbsolute( Texture.File ) ? NormalizeSlashes( Texture.File ) : NormalizeSlashes( SourceDirectory / Texture.File );
			if ( !NFileSystem::FileExists( Absolute ) )
			{
				const FString SameFolder = NormalizeSlashes( SourceDirectory / GetFileName( Texture.File ) );
				if ( NFileSystem::FileExists( SameFolder ) ) Absolute = SameFolder;
				else AddWarning( Result, "the texture '" + Texture.File + "' was not found" );
			}
			Paths.Add( NModelExport::MakeRelativePath( OutputDirectory, Absolute ) );
		}
		return Paths;
	}
}

FString FModelExportResult::GetSummary() const
{
	if ( !bSuccess ) return FString::Printf( "%s: failed (%s)", *SourcePath, *Error );
	if ( bSkippedUpToDate ) return FString::Printf( "%s: up to date (%s)", *SourcePath, *OutputPath );
	const char* Kind = SavedType == EModelSaveType::AnimOnly ? "animations only" : ( SavedType == EModelSaveType::MeshOnly ? "mesh only" : "mesh and animations" );
	return FString::Printf( "%s -> %s (%s; %d mesh(es), %d animation(s), %d bone(s); %lld bytes; %.0f ms%s)", *SourcePath, *OutputPath, Kind, NumMeshes, NumAnimations, NumBones,
							static_cast<long long>( OutputBytes ), Milliseconds, bImported ? "; imported" : "" );
}

void FModelBatchExportResult::Add( const FModelExportResult& Result )
{
	Items.Add( Result );
	if ( !Result.bSuccess ) ++NumFailed;
	else if ( Result.bSkippedUpToDate ) ++NumSkipped;
	else ++NumExported;
}

FString FModelBatchExportResult::GetSummary() const
{
	return FString::Printf( "%d exported, %d up to date, %d failed", NumExported, NumSkipped, NumFailed );
}

FString NModelExport::MakeRelativePath( const FString& FromDirectory, const FString& ToPath )
{
	const auto Split = []( const FString& Path )
	{
		TArray<FString> Parts = NormalizeSlashes( Path ).ParseIntoArray( "/", true );
		Parts.RemoveAll( []( const FString& Part ) { return Part == "."; } );
		return Parts;
	};
	const TArray<FString> From = Split( FromDirectory );
	const TArray<FString> To = Split( ToPath );
	if ( To.IsEmpty() ) return ToPath;

	int32 Common = 0;
	while ( Common < From.Num() && Common < To.Num() - 1 && From[Common].Equals( To[Common], ESearchCase::IgnoreCase ) ) ++Common;
	// 最初の階層（ドライブなど）から違うなら、相対にできない
	if ( Common == 0 && ( !From.IsEmpty() ) && ( IsAbsolute( FromDirectory ) || IsAbsolute( ToPath ) ) ) return NormalizeSlashes( ToPath );

	FString Result;
	for ( int32 i = Common; i < From.Num(); ++i ) Result += "../";
	for ( int32 i = Common; i < To.Num(); ++i )
	{
		Result += To[i];
		if ( i + 1 < To.Num() ) Result += "/";
	}
	return Result;
}

FModelExportResult NModelExport::Export( IAssetBackend& Backend, const FString& SourceFullPath, const FModelExportOptions& Options )
{
	const int64 StartTick = NPlatformTime::GetTicks();
	FModelExportResult Result;
	Result.SourcePath = NormalizeSlashes( SourceFullPath );

	const EModelFileKind Kind = NModelFormat::Classify( Result.SourcePath );
	if ( Kind == EModelFileKind::MV1 ) return Fail( Result, "the file is already a .mv1" );
	if ( Kind == EModelFileKind::Unknown ) return Fail( Result, "the file format is not supported" );
	if ( !NFileSystem::FileExists( Result.SourcePath ) ) return Fail( Result, "the file does not exist" );

	Result.OutputPath = NormalizeSlashes( Options.OutputPath.IsEmpty() ? NModelFormat::GetMV1Path( Result.SourcePath ) : Options.OutputPath );

	// 出力が新しければ何もしない
	if ( !Options.bForce )
	{
		FFileTime SourceTime, OutputTime;
		if ( NFileSystem::GetLastWriteTime( Result.SourcePath, SourceTime ) && NFileSystem::GetLastWriteTime( Result.OutputPath, OutputTime ) && NModelFormat::IsOutputUpToDate( SourceTime, OutputTime ) )
		{
			Result.bSuccess = true;
			Result.bSkippedUpToDate = true;
			Result.OutputBytes = NFileSystem::GetFileSize( Result.OutputPath );
			return Result;
		}
	}

	const FString OutputDirectory = GetDirectory( Result.OutputPath );
	if ( !OutputDirectory.IsEmpty() && !NFileSystem::EnsureDirectory( OutputDirectory ) ) return Fail( Result, "could not create the output folder '" + OutputDirectory + "'" );

	// 読む: 取り込みは、いったん .x にして DxLib に読ませる
	int32 Handle = NAsset::InvalidHandle;
	FString TemporaryX;
	if ( Kind == EModelFileKind::Imported )
	{
		TArray<uint8> Bytes;
		if ( !ReadFileBytes( Result.SourcePath, Bytes ) ) return Fail( Result, "could not read the file" );

		FImportOptions ImportOptions;
		ImportOptions.AnimFps = Options.AnimFps;
		FImportedScene Scene;
		FString ImportError;
		if ( !NModelImport::ImportFromMemory( Bytes.GetData(), Bytes.Num(), Result.SourcePath, ImportOptions, Scene, ImportError ) ) return Fail( Result, "could not read it as a model: " + ImportError );
		const TArray<FString> Problems = NImportedScene::Validate( Scene );
		if ( !Problems.IsEmpty() ) return Fail( Result, "the imported data is inconsistent: " + Problems[0] );
		if ( Scene.Nodes.IsEmpty() ) return Fail( Result, "the file has no nodes (no model)" );
		if ( !Scene.HasMesh() && Scene.Animations.IsEmpty() ) AddWarning( Result, "the file has neither a mesh nor an animation" );

		NImportedScene::ConvertToLeftHanded( Scene );
		const FString OutputBase = GetBaseName( Result.OutputPath );
		const TArray<FString> TexturePaths = PrepareTextures( Scene, GetDirectory( Result.SourcePath ), OutputDirectory, OutputBase, Result );
		const FString Text = NXFile::Write( Scene, TexturePaths );

		TemporaryX = ( OutputDirectory.IsEmpty() ? FString() : OutputDirectory + "/" ) + OutputBase + ".import.tmp.x";
		if ( !WriteFileBytes( TemporaryX, *Text, static_cast<size_t>( Text.Len() ) ) ) return Fail( Result, "could not write the temporary file '" + TemporaryX + "'" );
		Handle = Backend.LoadModel( TemporaryX, false );
		Result.bImported = true;
	}
	else
	{
		Handle = Backend.LoadModel( Result.SourcePath, false );
	}
	if ( Handle < 0 )
	{
		if ( !TemporaryX.IsEmpty() ) NFileSystem::RemoveFile( TemporaryX );
		return Fail( Result, Kind == EModelFileKind::Imported ? "DxLib could not read the converted data" : "DxLib could not read the file" );
	}

	Result.NumMeshes = Backend.GetModelMeshCount( Handle );
	Result.NumAnimations = Backend.GetModelAnimCount( Handle );
	Result.NumBones = Backend.GetModelFrameCount( Handle );

	FModelSaveOptions Save;
	Save.bCompress = Options.bCompress;
	Save.Type = Options.bAutoSaveType ? ( Result.NumMeshes > 0 ? EModelSaveType::Normal : EModelSaveType::AnimOnly ) : Options.SaveType;
	Result.SavedType = Save.Type;

	FString Failure;
	if ( Result.NumMeshes == 0 && Result.NumAnimations == 0 ) Failure = "the file has neither a mesh nor an animation, so there is nothing to save";
	else if ( Save.Type == EModelSaveType::AnimOnly && Result.NumAnimations == 0 ) Failure = "the file has no animations to save";
	else if ( Save.Type == EModelSaveType::MeshOnly && Result.NumMeshes == 0 ) Failure = "the file has no mesh to save";

	if ( Failure.IsEmpty() )
	{
		FString SaveWarning;
		if ( !Backend.SaveModelToMV1( Handle, Result.OutputPath, Save, SaveWarning ) ) Failure = "DxLib could not save the .mv1 file";
		else if ( !SaveWarning.IsEmpty() ) AddWarning( Result, SaveWarning );
	}

	Backend.DeleteModel( Handle );
	if ( !TemporaryX.IsEmpty() ) NFileSystem::RemoveFile( TemporaryX );
	if ( !Failure.IsEmpty() ) return Fail( Result, Failure );

	Result.bSuccess = true;
	Result.OutputBytes = NFileSystem::GetFileSize( Result.OutputPath );
	Result.Milliseconds = static_cast<float32>( NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - StartTick ) );
	LOG( LogModelExport, Log, "%s", *Result.GetSummary() );
	return Result;
}
