// SPDX-License-Identifier: MIT
#include "AssetReferences.h"

#include "Library/Core/Collection/Sort.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/World/Definition/WorldDefinition.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	// 参照を持ちうるデータアセット
	const char* const ReferencingExtensions[] = { ".gxsprite", ".gxmodelanim", ".gxworld", ".gxeffect", ".gxblackboard", ".gxstate", ".gxbt", ".gxparts" };

	TArray<FString> ListDataAssets( CAssetSubsystem& Assets )
	{
		TArray<FString> Extensions;
		for ( const char* Extension : ReferencingExtensions )
		{
			Extensions.Add( Extension );
		}
		TArray<FString> Files = NFileSystem::ListFilesRecursive( Assets.GetRootDirectory(), Extensions );
		NAlgo::Sort( Files.GetData(), Files.Num() );
		return Files;
	}

	FString ExtensionOf( const FString& Path )
	{
		const int32 Dot = Path.FindLast( "." );
		const int32 Slash = Path.FindLast( "/" );
		return Dot > Slash && Dot >= 0 ? Path.Mid( Dot ) : FString();
	}

	bool IsInsideRoot( const FString& Path )
	{
		if ( Path.IsEmpty() || Path.Contains( ":" ) || Path.StartsWith( "/" ) || Path.StartsWith( "\\" ) )
		{
			return false;
		}
		const FString Normalized = Path.Replace( "\\", "/" );
		return !Normalized.StartsWith( "../" ) && !Normalized.Contains( "/../" ) && !Normalized.EndsWith( "/.." ) && Normalized != "..";
	}
}

int32 NAssetReferences::CountPathStrings( const NlohmannJson& Json, const FString& NormalizedTarget )
{
	if ( Json.is_string() )
	{
		return NWorld::NormalizeAssetPath( FString( Json.get_ref<const std::string&>().c_str() ) ) == NormalizedTarget ? 1 : 0;
	}
	int32 Count = 0;
	if ( Json.is_object() || Json.is_array() )
	{
		for ( const NlohmannJson& Child : Json )
		{
			Count += CountPathStrings( Child, NormalizedTarget );
		}
	}
	return Count;
}

int32 NAssetReferences::ReplacePathStrings( NlohmannJson& Json, const FString& NormalizedTarget, const FString& NewPath )
{
	if ( Json.is_string() )
	{
		if ( NWorld::NormalizeAssetPath( FString( Json.get_ref<const std::string&>().c_str() ) ) == NormalizedTarget )
		{
			Json = std::string( *NewPath.Replace( "\\", "/" ) );
			return 1;
		}
		return 0;
	}
	int32 Count = 0;
	if ( Json.is_object() || Json.is_array() )
	{
		for ( NlohmannJson& Child : Json )
		{
			Count += ReplacePathStrings( Child, NormalizedTarget, NewPath );
		}
	}
	return Count;
}

TArray<FAssetReference> NAssetReferences::FindReferences( CAssetSubsystem& Assets, const FString& AssetPath )
{
	TArray<FAssetReference> Result;
	if ( AssetPath.IsEmpty() )
	{
		return Result;
	}
	const FString Target = NWorld::NormalizeAssetPath( AssetPath );
	const FString Root = Assets.GetRootDirectory();
	for ( const FString& File : ListDataAssets( Assets ) )
	{
		if ( NWorld::NormalizeAssetPath( File ) == Target )
		{
			continue;
		}
		NJson::CJsonObject Document;
		if ( !Document.LoadFromFile( Root / File ) )
		{
			continue;
		}
		const int32 Count = CountPathStrings( Document.GetRoot(), Target );
		if ( Count > 0 )
		{
			FAssetReference Reference;
			Reference.FilePath = File;
			Reference.Count = Count;
			Result.Add( Reference );
		}
	}
	return Result;
}

FAssetRenameResult NAssetReferences::RenameAsset( CAssetSubsystem& Assets, const FString& OldPath, const FString& NewPath )
{
	FAssetRenameResult Result;
	const FString Old = OldPath.Replace( "\\", "/" );
	const FString New = NewPath.Replace( "\\", "/" );
	if ( Old.IsEmpty() || New.IsEmpty() )
	{
		Result.Error = "The path is empty.";
		return Result;
	}
	if ( !IsInsideRoot( Old ) || !IsInsideRoot( New ) )
	{
		Result.Error = "The path must be relative to the asset root (no drive letters, no '..').";
		return Result;
	}
	if ( !ExtensionOf( Old ).Equals( ExtensionOf( New ), ESearchCase::IgnoreCase ) )
	{
		Result.Error = "The extension cannot be changed (" + ExtensionOf( Old ) + ").";
		return Result;
	}
	const FString Root = Assets.GetRootDirectory();
	if ( !NFileSystem::FileExists( Root / Old ) )
	{
		Result.Error = "'" + Old + "' was not found.";
		return Result;
	}
	if ( NWorld::NormalizeAssetPath( Old ) == NWorld::NormalizeAssetPath( New ) && Old == New )
	{
		Result.Error = "The new path is the same as the old one.";
		return Result;
	}
	const bool bOnlyCaseChanged = NWorld::NormalizeAssetPath( Old ) == NWorld::NormalizeAssetPath( New );
	if ( !bOnlyCaseChanged && NFileSystem::FileExists( Root / New ) )
	{
		Result.Error = "'" + New + "' already exists.";
		return Result;
	}

	// 書き換える前に、参照を集める（ファイルを移す前の状態で）
	const FString Target = NWorld::NormalizeAssetPath( Old );
	struct FPending
	{
		FString File;
		NJson::CJsonObject Document;
		int32 Count = 0;
	};
	TArray<TSharedPtr<FPending>> Pending;
	for ( const FString& File : ListDataAssets( Assets ) )
	{
		if ( NWorld::NormalizeAssetPath( File ) == Target )
		{
			continue;
		}
		TSharedPtr<FPending> Item = MakeShared<FPending>();
		Item->File = File;
		if ( !Item->Document.LoadFromFile( Root / File ) )
		{
			continue;
		}
		Item->Count = ReplacePathStrings( Item->Document.GetRoot(), Target, New );
		if ( Item->Count > 0 )
		{
			Pending.Add( Item );
		}
	}

	// ファイルを移す
	NFileSystem::EnsureDirectory( ( Root / New ).Left( ( Root / New ).FindLast( "/" ) ) );
	if ( !NFileSystem::RenameFile( Root / Old, Root / New ) )
	{
		Result.Error = "The file could not be moved.";
		return Result;
	}

	// 参照を書き換える。1 つでも書き込みに失敗したら、書き換え済みのファイルも元のパスへ戻し、ファイルも移し戻して、失敗にする
	const FString NewTarget = NWorld::NormalizeAssetPath( New );
	for ( int32 Index = 0; Index < Pending.Num(); ++Index )
	{
		const TSharedPtr<FPending>& Item = Pending[Index];
		if ( !Item->Document.SaveToFile( Root / Item->File ) )
		{
			for ( int32 Done = 0; Done < Index; ++Done )
			{
				ReplacePathStrings( Pending[Done]->Document.GetRoot(), NewTarget, Old );
				Pending[Done]->Document.SaveToFile( Root / Pending[Done]->File );
			}
			NFileSystem::RenameFile( Root / New, Root / Old );
			Result.Error = "'" + Item->File + "' could not be written. Nothing was changed.";
			Result.Updated.Reset();
			return Result;
		}
		FAssetReference Updated;
		Updated.FilePath = Item->File;
		Updated.Count = Item->Count;
		Result.Updated.Add( Updated );
	}
	Result.bSuccess = true;
	return Result;
}

bool NAssetReferences::DeleteAsset( CAssetSubsystem& Assets, const FString& AssetPath, FString* OutError )
{
	const FString Path = AssetPath.Replace( "\\", "/" );
	const auto Fail = [&]( const FString& Message )
	{
		if ( OutError != nullptr )
		{
			*OutError = Message;
		}
		return false;
	};
	if ( !IsInsideRoot( Path ) )
	{
		return Fail( "The path must be relative to the asset root." );
	}
	const FString Full = Assets.GetRootDirectory() / Path;
	if ( !NFileSystem::FileExists( Full ) )
	{
		return Fail( "'" + Path + "' was not found." );
	}
	if ( !NFileSystem::RemoveFile( Full ) )
	{
		return Fail( "The file could not be deleted." );
	}
	return true;
}
