// SPDX-License-Identifier: MIT
#include "StaticModelStore.h"
#include "Library/Framework/Asset/Model/Import/StaticModelBuilder.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

FStaticModelStore::FResource::~FResource()
{
	if ( TextureBackend )
	{
		for ( int32 Texture : OwnedTextures )
		{
			TextureBackend->DeleteTexture( Texture );
		}
	}
}

int32 FStaticModelStore::Create( const FImportedScene& Scene, const FString& SourcePath, ITextureBackend& Textures, FString& Error )
{
	if ( Instances.Num() >= 128 || NextHandle == TNumericLimits<int32>::Max() )
	{
		Error = "Static model instance limit exceeded";
		return -1;
	}
	auto Resource = MakeShared<FResource>(); // 読み込み途中の所有も局所化する。
	Resource->TextureBackend = &Textures;
	if ( !FStaticModelBuilder::Build( Scene, Resource->Definition, Error ) )
	{
		return -1;
	}
	auto& Surface = Resource->Definition.Surface; // 成功前は公開しない面データ。
	Resource->Bytes = static_cast<int64>( Surface.Vertices.Num() ) * ( sizeof( FMeshVertex ) + sizeof( FDrawVertex3D ) )
		+ static_cast<int64>( Surface.Indices.Num() ) * sizeof( uint32 ) + static_cast<int64>( Surface.Sections.Num() ) * ( sizeof( FStaticModelSurfaceSection ) + sizeof( FStaticModelDrawSection ) );
	int64 TotalBytes = Resource->Bytes; // 同じ共有形状は一度だけ数える。
	TSet<const FResource*> Seen; // 複製分を重複加算しない。
	for ( const auto& Pair : Instances )
	{
		const FResource* Existing = Pair.Value.Resource.Get(); // 登録済みの共有形状。
		if ( !Seen.Contains( Existing ) )
		{
			Seen.Add( Existing );
			TotalBytes += Existing->Bytes;
		}
	}
	if ( TotalBytes > 64 * 1024 * 1024 )
	{
		Error = "Static model geometry memory limit exceeded";
		return -1;
	}
	TMap<int32, int32> LoadedTextures; // 同じ元画像を一度だけ読み込む。
	for ( const auto& Section : Surface.Sections )
	{
		if ( Section.MaterialIndex >= Scene.Materials.Num() )
		{
			continue; // 材質未指定用の白。
		}
		const int32 TextureIndex = Scene.Materials[Section.MaterialIndex].Texture; // 元の拡散画像番号。
		if ( TextureIndex < 0 )
		{
			continue;
		}
		if ( TextureIndex >= Scene.Textures.Num() )
		{
			Error = "Invalid diffuse texture index";
			return -1;
		}
		int32 Texture = -1; // モデルが所有する画像の登録番号。
		if ( const int32* Existing = LoadedTextures.Find( TextureIndex ) )
		{
			Texture = *Existing;
		}
		else
		{
			const auto& Input = Scene.Textures[TextureIndex]; // 埋め込み画像か外部参照。
			if ( !Input.EmbeddedData.IsEmpty() )
			{
				Texture = Textures.CreateTextureFromMemory( Input.EmbeddedData.GetData(), Input.EmbeddedData.Num() );
			}
			else
			{
				FString Path = Input.File.Replace( "\\", "/" ); // 相対パスは元モデルのフォルダーを基準にする。
				if ( !Path.StartsWith( "/" ) && !( Path.Len() >= 2 && Path[1] == ':' ) )
				{
					const int32 Separator = NMath::Max( SourcePath.FindLast( "/" ), SourcePath.FindLast( "\\" ) ); // 元ファイルのフォルダー。
					Path = ( Separator >= 0 ? SourcePath.Left( Separator + 1 ) : FString() ) + Path;
				}
				Texture = Textures.LoadTexture( Path, false );
			}
			if ( Texture < 0 )
			{
				Error = FString( "Cannot load static model texture: " ) + Input.File;
				return -1;
			}
			Resource->OwnedTextures.Add( Texture );
			LoadedTextures.Add( TextureIndex, Texture );
		}
		Surface.Materials[Section.MaterialIndex].BorrowedDiffuseTexture = Texture;
		int32 Width = 0, Height = 0; // 画像の実寸をメタデータへ反映する。
		if ( Textures.GetTextureSize( Texture, Width, Height ) )
		{
			Resource->Definition.Structure.Textures[TextureIndex].Width = Width;
			Resource->Definition.Structure.Textures[TextureIndex].Height = Height;
		}
	}
	Resource->Vertices.SetNum( Surface.Vertices.Num() );
	for ( int32 Index = 0; Index < Surface.Vertices.Num(); ++Index )
	{
		const auto& Input = Surface.Vertices[Index]; // 材質色を含むreference頂点。
		auto& Output = Resource->Vertices[Index]; // API共通のローカル頂点。
		FMemory::Memcpy( Output.Position, Input.Position, sizeof( Input.Position ) );
		FMemory::Memcpy( Output.Normal, Input.Normal, sizeof( Input.Normal ) );
		FMemory::Memcpy( Output.Diffuse, Input.Color, sizeof( Input.Color ) );
		Output.U = Input.U;
		Output.V = Input.V;
	}
	for ( const auto& Section : Surface.Sections )
	{
		Resource->Sections.Add( { Section.FirstIndex, Section.NumIndices, Surface.Materials[Section.MaterialIndex].BorrowedDiffuseTexture } );
	}
	const int32 Handle = NextHandle++; // 完成してから登録を公開する。
	Instances.Add( Handle, FInstance { Resource, 1 } );
	Error = FString();
	return Handle;
}

int32 FStaticModelStore::Duplicate( int32 Source )
{
	const FInstance* Instance = Instances.Find( Source ); // 複写前の個体。
	if ( !Instance || Instances.Num() >= 128 || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	const FInstance Copy = *Instance; // 表の再確保前に共有と状態を固定する。
	const int32 Handle = NextHandle++; // 別個体の番号。
	Instances.Add( Handle, Copy );
	return Handle;
}

void FStaticModelStore::Delete( int32 Handle )
{
	Instances.Remove( Handle );
}

void FStaticModelStore::Reset()
{
	Instances.Reset();
}

int32 FStaticModelStore::Num() const
{
	return Instances.Num();
}

bool FStaticModelStore::GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out )
{
	const auto* Instance = Instances.Find( Handle ); // 描画まで生存する個体。
	if ( !Instance )
	{
		return false;
	}
	const auto& Data = *Instance->Resource; // 書き換えない共有形状。
	Out = { Data.Vertices.GetData(), Data.Vertices.Num(), Data.Definition.Surface.Indices.GetData(), Data.Definition.Surface.Indices.Num(), Data.Sections.GetData(), Data.Sections.Num(), Instance->Opacity };
	return true;
}

const FStaticModelDefinition* FStaticModelStore::GetDefinition( int32 Handle ) const
{
	const auto* Instance = Instances.Find( Handle ); // 定義の借用元。
	return Instance ? &Instance->Resource->Definition : nullptr;
}

bool FStaticModelStore::GetSurface( int32 Handle, FStaticModelSurfaceData& Out ) const
{
	const auto* Definition = GetDefinition( Handle ); // 有効な登録だけ返す。
	if ( !Definition )
	{
		return false;
	}
	Out = Definition->Surface;
	return true;
}

bool FStaticModelStore::GetOpacity( int32 Handle, float32& Out ) const
{
	const auto* Instance = Instances.Find( Handle ); // 個体別の状態。
	if ( !Instance )
	{
		return false;
	}
	Out = Instance->Opacity;
	return true;
}

bool FStaticModelStore::SetOpacity( int32 Handle, float32 Opacity )
{
	auto* Instance = Instances.Find( Handle ); // 共有形状は変更しない。
	if ( !Instance || !NMath::IsFinite( Opacity ) || Opacity < 0 || Opacity > 1 )
	{
		return false;
	}
	Instance->Opacity = Opacity;
	return true;
}

int64 FStaticModelStore::EstimateBytes( int32 Handle ) const
{
	const auto* Instance = Instances.Find( Handle ); // この個体が共有する形状。
	return Instance ? Instance->Resource->Bytes : 0;
}
