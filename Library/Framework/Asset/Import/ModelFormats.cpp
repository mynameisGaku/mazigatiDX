#include "ModelFormats.h"

#include <algorithm>

#include "Library/Core/Collection/Set.h"

namespace
{
	const TArray<FString>& GetNativeExtensions()
	{
		static const TArray<FString> Extensions = { ".x", ".pmd", ".pmx", ".mqo", ".mqoz", ".vmd" };
		return Extensions;
	}

	const TArray<FString>& GetImportedExtensions()
	{
		static const TArray<FString> Extensions = { ".fbx", ".obj" };
		return Extensions;
	}

	// ".png" のようなドットつきの拡張子（小文字）。無ければ空。パスの途中のドット（"a.b/c"）は拡張子ではない
	FString GetExtension( const FString& Path )
	{
		const int32 Dot = Path.FindLast( "." );
		if ( Dot == INDEX_NONE ) return FString();
		const int32 Separator = std::max( Path.FindLast( "/" ), Path.FindLast( "\\" ) );
		if ( Separator > Dot ) return FString();
		return Path.Mid( Dot ).ToLower();
	}
}

EModelFileKind NModelFormat::Classify( const FString& Path )
{
	const FString Extension = GetExtension( Path );
	if ( Extension == ".mv1" ) return EModelFileKind::MV1;
	if ( GetNativeExtensions().Contains( Extension ) ) return EModelFileKind::DxLibNative;
	if ( GetImportedExtensions().Contains( Extension ) ) return EModelFileKind::Imported;
	return EModelFileKind::Unknown;
}

bool NModelFormat::CanConvertToMV1( const FString& Path )
{
	const EModelFileKind Kind = Classify( Path );
	return Kind == EModelFileKind::DxLibNative || Kind == EModelFileKind::Imported;
}

FString NModelFormat::GetMV1Path( const FString& SourcePath )
{
	const FString Extension = GetExtension( SourcePath );
	if ( Extension.IsEmpty() ) return SourcePath + ".mv1";
	return SourcePath.Left( SourcePath.Len() - Extension.Len() ) + ".mv1";
}

const TArray<FString>& NModelFormat::GetConvertibleExtensions()
{
	static const TArray<FString> Extensions = []
	{
		TArray<FString> All = GetNativeExtensions();
		All.Append( GetImportedExtensions() );
		return All;
	}();
	return Extensions;
}

const TArray<FString>& NModelFormat::GetAllModelExtensions()
{
	static const TArray<FString> Extensions = []
	{
		TArray<FString> All = { ".mv1" };
		All.Append( GetConvertibleExtensions() );
		return All;
	}();
	return Extensions;
}

int64 NModelFormat::ToSortKey( const FFileTime& Time )
{
	int64 Key = Time.Year;
	Key = Key * 100 + Time.Month;
	Key = Key * 100 + Time.Day;
	Key = Key * 100 + Time.Hour;
	Key = Key * 100 + Time.Minute;
	Key = Key * 100 + Time.Second;
	return Key;
}

bool NModelFormat::IsOutputUpToDate( const FFileTime& SourceTime, const FFileTime& OutputTime )
{
	if ( SourceTime.Year == 0 || OutputTime.Year == 0 ) return false;
	return ToSortKey( OutputTime ) >= ToSortKey( SourceTime );
}

FBoneMatch NModelCompat::MatchBones( const TArray<FString>& ModelBones, const TArray<FString>& AnimBones )
{
	TSet<FString> Available;
	for ( const FString& Name : ModelBones ) Available.Add( Name );

	FBoneMatch Result;
	for ( const FString& Name : AnimBones )
	{
		++Result.NumAnimBones;
		if ( Available.Contains( Name ) ) ++Result.NumMatched;
		else if ( Result.Missing.Num() < MaxMissingNames ) Result.Missing.Add( Name );
	}
	return Result;
}
