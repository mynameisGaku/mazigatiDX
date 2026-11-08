// SPDX-License-Identifier: MIT
#include "MaterialSubsystem.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC( LogMaterial, Log )

REGISTER_SUBSYSTEM( CMaterialSubsystem )

CMaterialSubsystem::CMaterialSubsystem() = default;

CMaterialSubsystem::~CMaterialSubsystem() = default;

void CMaterialSubsystem::Deinitialize()
{
	Loaded.Empty();
	ParameterCollections.Empty();
	Alive.Reset();
}

ETickGroup CMaterialSubsystem::GetTickGroup() const
{
	return ETickGroup::PreUpdate;
}

void CMaterialSubsystem::Tick( float32 InDeltaTime )
{
	DeltaTime = InDeltaTime;
	Time += InDeltaTime;
	if ( HotReloadInterval > 0.0f )
	{
		HotReloadTimer += InDeltaTime;
		if ( HotReloadTimer >= HotReloadInterval )
		{
			HotReloadTimer = 0.0f;
			ReloadChanged();
		}
	}
}

FMaterialFrameState CMaterialSubsystem::MakeFrameState() const
{
	FMaterialFrameState State;
	State.Time = Time;
	State.DeltaTime = DeltaTime;
	return State;
}

TSharedPtr<CMaterialParameterCollection> CMaterialSubsystem::FindParameterCollection( const FString& Name ) const
{
	for ( int32 Index = 0; Index < ParameterCollections.Num(); ++Index )
	{
		if ( ParameterCollections[Index]->GetName() == Name )
		{
			return ParameterCollections[Index];
		}
	}
	return TSharedPtr<CMaterialParameterCollection>();
}

TSharedPtr<CMaterialParameterCollection> CMaterialSubsystem::GetParameterCollection( const FString& Name )
{
	TSharedPtr<CMaterialParameterCollection> Found = FindParameterCollection( Name );
	if ( !Found )
	{
		Found = MakeShared<CMaterialParameterCollection>( Name );
		ParameterCollections.Add( Found );
	}
	return Found;
}

bool CMaterialSubsystem::ReadText( const FString& Path, FString& OutText ) const
{
	OutText.Empty();
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return false;
	}
	const TOptional<FString> Resolved = Assets->ResolvePath( Path );
	if ( !Resolved.IsSet() )
	{
		return false;
	}
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Resolved.GetValue(), Bytes ) )
	{
		return false;
	}
	int32 Start = 0;
	if ( Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF )
	{
		Start = 3;
	}
	TArray<char> Chars;
	for ( int32 Index = Start; Index < Bytes.Num(); ++Index )
	{
		Chars.Add( static_cast<char>( Bytes[Index] ) );
	}
	Chars.Add( '\0' );
	OutText = FString( Chars.GetData() );
	return true;
}

TSharedPtr<CMaterialParameterCollection> CMaterialSubsystem::LoadParameterCollection( const FString& Path )
{
	FString Text;
	if ( !ReadText( Path, Text ) )
	{
		LOG( LogMaterial, Warning, "Cannot read the material collection '%s'", *Path );
		return TSharedPtr<CMaterialParameterCollection>();
	}
	CMaterialParameterCollection Loaded;
	FString Error;
	if ( !CMaterialParameterCollection::FromJson( Text, Loaded, &Error ) )
	{
		LOG( LogMaterial, Warning, "The material collection '%s' is not valid: %s", *Path, *Error );
		return TSharedPtr<CMaterialParameterCollection>();
	}
	// 名前は、ファイルの Name（無ければ、パスの、ファイル名の部分）
	FString Name = Loaded.GetName();
	if ( Name.IsEmpty() )
	{
		Name = Path;
		const int32 Slash = Path.FindLast( "/" );
		if ( Slash >= 0 )
		{
			Name = Path.Mid( Slash + 1 );
		}
		const int32 Dot = Name.FindLast( "." );
		if ( Dot > 0 )
		{
			Name = Name.Left( Dot );
		}
	}
	TSharedPtr<CMaterialParameterCollection> Target = GetParameterCollection( Name );
	for ( int32 Index = 0; Index < Loaded.Num(); ++Index )
	{
		if ( Loaded.IsScalarAt( Index ) )
		{
			Target->SetScalar( Loaded.GetNameAt( Index ), Loaded.GetValueAt( Index ).x );
		}
		else
		{
			Target->SetVector( Loaded.GetNameAt( Index ), Loaded.GetValueAt( Index ) );
		}
	}
	return Target;
}

void CMaterialSubsystem::AttachResolver( CMaterial& Material )
{
	const TWeakPtr<bool> Token = Alive;
	Material.SetParameterCollectionResolver( [this, Token]( const FString& Name ) -> TSharedPtr<CMaterialParameterCollection >
	{
		return Token.IsValid() ? FindParameterCollection( Name ) : TSharedPtr<CMaterialParameterCollection>();
	} );
}

TSharedPtr<CMaterial> CMaterialSubsystem::CreateMaterial( const FMaterialDefinition& Definition )
{
	TSharedPtr<CMaterial> Material = CMaterial::Create( Definition, Capabilities );
	AttachResolver( *Material );
	return Material;
}

bool CMaterialSubsystem::Build( FLoaded& Entry, const FString& Text, bool bReload, FString* OutError )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Entry.bInstance )
	{
		FMaterialInstanceDefinition Definition;
		if ( !FMaterialInstanceDefinition::FromJson( Text, Definition, OutError ) )
		{
			return false;
		}
		const TSharedPtr<CMaterial> Parent = LoadMaterial( Definition.ParentPath );
		if ( !Parent )
		{
			if ( OutError != nullptr )
			{
				*OutError = FString::Printf( "the parent material '%s' could not be loaded", *Definition.ParentPath );
			}
			return false;
		}
		TSharedPtr<CMaterial> Instance = bReload && Entry.Material && Entry.Material->GetParent() == Parent ? Entry.Material : CMaterial::CreateInstance( Parent );
		Instance->ClearOverrides();
		TArray<FString> Missing;
		Definition.Apply( *Instance, Assets, &Missing );
		for ( int32 Index = 0; Index < Missing.Num(); ++Index )
		{
			LOG( LogMaterial, Warning, "The instance '%s' overrides '%s', which the parent does not have (or has with another type)", *Entry.Path, *Missing[Index] );
		}
		Entry.Material = Instance;
		return true;
	}
	FMaterialDefinition Definition;
	if ( !FMaterialDefinition::FromJson( Text, Definition, OutError ) )
	{
		return false;
	}
	if ( bReload && Entry.Material )
	{
		Entry.Material->Reload( Definition );
	}
	else
	{
		Entry.Material = CreateMaterial( Definition );
	}
	if ( Assets != nullptr )
	{
		Entry.Material->LoadTextures( *Assets );
	}
	return true;
}

TSharedPtr<CMaterial> CMaterialSubsystem::LoadMaterial( const FString& Path )
{
	for ( int32 Index = 0; Index < Loaded.Num(); ++Index )
	{
		if ( Loaded[Index].Path == Path )
		{
			return Loaded[Index].Material;
		}
	}
	FString Text;
	if ( !ReadText( Path, Text ) )
	{
		LOG( LogMaterial, Warning, "Cannot read the material '%s'", *Path );
		return TSharedPtr<CMaterial>();
	}
	FLoaded Entry;
	Entry.Path = Path;
	Entry.Text = Text;
	Entry.bInstance = Path.EndsWith( ".gxmaterialinstance" );
	FString Error;
	if ( !Build( Entry, Text, false, &Error ) )
	{
		LOG( LogMaterial, Warning, "The material '%s' is not valid: %s", *Path, *Error );
		return TSharedPtr<CMaterial>();
	}
	if ( !Entry.Material->IsValid() )
	{
		const TArray<FMaterialDiagnostic> Diagnostics = Entry.Material->GetDiagnostics();
		for ( int32 Index = 0; Index < Diagnostics.Num(); ++Index )
		{
			if ( Diagnostics[Index].Severity == EMaterialSeverity::Error )
			{
				LOG( LogMaterial, Warning, "The material '%s' has an error: %s", *Path, *Diagnostics[Index].Message );
			}
		}
	}
	Loaded.Add( Entry );
	return Entry.Material;
}

int32 CMaterialSubsystem::ReloadChanged()
{
	int32 Count = 0;
	// 読み込み順に調べる（親のマテリアルは、インスタンスより先に読まれている）
	for ( int32 Index = 0; Index < Loaded.Num(); ++Index )
	{
		FString Text;
		if ( !ReadText( Loaded[Index].Path, Text ) || Text == Loaded[Index].Text )
		{
			continue;
		}
		// 壊れた中身は、覚えておく（同じ失敗を、毎回やり直さない。直されたら、中身が変わるので、また試す）
		FLoaded& Entry = Loaded[Index];
		Entry.Text = Text;
		FString Error;
		if ( Build( Entry, Text, true, &Error ) )
		{
			++Count;
			LOG( LogMaterial, Log, "Reloaded the material '%s'", *Entry.Path );
		}
		else
		{
			LOG( LogMaterial, Warning, "The material '%s' was changed but is not valid (the old one stays): %s", *Entry.Path, *Error );
		}
	}
	return Count;
}
