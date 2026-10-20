#include "WorldAsset.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"

CWorldAsset::CWorldAsset( const FString& InPath, const FWorldDefinition& InDefinition, const TArray<FString>& InLoadWarnings )
	: CAsset( EAssetType::World, InPath )
	, Definition( MakeShared<FWorldDefinition>( InDefinition ) )
	, LoadWarnings( InLoadWarnings )
{
}

FString CWorldAsset::Describe() const
{
	int32 NumEnabled = 0;
	for ( const FWorldObject& Object : Definition->Objects )
	{
		if ( Object.bEnabled ) ++NumEnabled;
	}
	return FString::Printf( "%d object(s)%s, %d collision rule(s)", Definition->Objects.Num(), NumEnabled != Definition->Objects.Num() ? *FString::Printf( " (%d enabled)", NumEnabled ) : "",
							Definition->Settings.CollisionRules.Num() );
}

int64 CWorldAsset::GetMemoryBytes() const
{
	// おおよその推定（オブジェクトの本体と、文字列・タグ・プロパティの分）
	int64 Bytes = static_cast<int64>( sizeof( FWorldDefinition ) );
	for ( const FWorldObject& Object : Definition->Objects )
	{
		Bytes += static_cast<int64>( sizeof( FWorldObject ) ) + Object.Name.Len() + static_cast<int64>( Object.Tags.Num() ) * 32 + static_cast<int64>( Object.Props.Num() ) * static_cast<int64>( sizeof( FWorldProp ) );
	}
	return Bytes;
}

bool FAssetWorldQuery::Exists( const FString& Path ) const
{
	if ( Assets.ResolvePath( Path ).IsSet() ) return true;
	return NModelFormat::CanConvertToMV1( Path ) && Assets.ResolvePath( NModelFormat::GetMV1Path( Path ) ).IsSet();
}
