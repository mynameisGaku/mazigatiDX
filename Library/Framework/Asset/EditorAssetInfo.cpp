#include "EditorAssetInfo.h"

#include "Library/Framework/Asset/AssetManager.h"

FAssetEditorInfo::FAssetEditorInfo( CAssetManager& InAssets )
	: Assets( InAssets )
	, Query( InAssets )
{
}

bool FAssetEditorInfo::Exists( const FString& Path ) const
{
	return Query.Exists( Path );
}

const FAssetEditorInfo::FModelEntry& FAssetEditorInfo::FindModel( const FString& Path ) const
{
	if ( const FModelEntry* Found = Models.Find( Path ) ) return *Found;

	FModelEntry Entry;
	if ( !Path.IsEmpty() && Query.Exists( Path ) )
	{
		Entry.Model = Assets.LoadModel( Path );
		if ( Entry.Model.IsValid() && Entry.Model->HasMesh() ) Entry.Instance = Entry.Model->CreateInstance();
	}
	return Models.Add( Path, Entry );
}

bool FAssetEditorInfo::GetModelBounds( const FString& Path, FVector3& OutMin, FVector3& OutMax ) const
{
	const FModelEntry& Entry = FindModel( Path );
	return Entry.Model.IsValid() && Entry.Model->GetBounds( OutMin, OutMax );
}

bool FAssetEditorInfo::GetTextureSize( const FString& Path, int32& OutWidth, int32& OutHeight ) const
{
	const TSharedPtr<CTexture> Texture = GetTexture( Path );
	if ( !Texture.IsValid() ) return false;
	OutWidth = Texture->GetWidth();
	OutHeight = Texture->GetHeight();
	return OutWidth > 0 && OutHeight > 0;
}

const FEditorCollisionMesh* FAssetEditorInfo::GetCollisionMesh( const FString& Path ) const
{
	if ( const TSharedPtr<FEditorCollisionMesh>* Found = Meshes.Find( Path ) ) return Found->Get();
	TSharedPtr<FEditorCollisionMesh> Mesh;
	const FModelEntry& Entry = FindModel( Path );
	if ( Entry.Model.IsValid() )
	{
		TSharedPtr<FEditorCollisionMesh> Loaded = MakeShared<FEditorCollisionMesh>();
		if ( Entry.Model->GetCollisionMesh( Loaded->Vertices, Loaded->Indices ) ) Mesh = Loaded;
	}
	Meshes.Add( Path, Mesh );
	return Mesh.Get();
}

const FWorldDefinition* FAssetEditorInfo::GetWorld( const FString& Path ) const
{
	const FString Key = NWorld::NormalizeAssetPath( Path );
	if ( const TSharedPtr<CWorldAsset>* Found = Worlds.Find( Key ) ) return Found->IsValid() ? &( *Found )->GetDefinition() : nullptr;
	TSharedPtr<CWorldAsset> Loaded;
	if ( !Path.IsEmpty() && Query.Exists( Path ) ) Loaded = Assets.LoadWorld( Path );
	Worlds.Add( Key, Loaded );
	return Loaded.IsValid() ? &Loaded->GetDefinition() : nullptr;
}

TSharedPtr<CModelInstance> FAssetEditorInfo::GetModelInstance( const FString& Path ) const
{
	return FindModel( Path ).Instance;
}

TSharedPtr<CModel> FAssetEditorInfo::GetModelSource( const FString& Path ) const
{
	return FindModel( Path ).Model;
}

TSharedPtr<CTexture> FAssetEditorInfo::GetTexture( const FString& Path ) const
{
	if ( const TSharedPtr<CTexture>* Found = Textures.Find( Path ) ) return *Found;
	TSharedPtr<CTexture> Texture;
	if ( !Path.IsEmpty() && Query.Exists( Path ) ) Texture = Assets.LoadTexture( Path );
	Textures.Add( Path, Texture );
	return Texture;
}

void FAssetEditorInfo::Clear()
{
	Models.Empty();
	Textures.Empty();
	Meshes.Empty();
	Worlds.Empty();
}
