// SPDX-License-Identifier: MIT
#include "AIPartsCatalog.h"

#include "Library/Core/Collection/Sort.h"
#include "Library/Framework/AI/Registry/AIParts.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

namespace
{
	//=========================================================================
	// 名前とスキーマだけの部品（エディタ専用の実行ファイルが、ゲームの部品の代わりに登録する。何もしない）
	//=========================================================================
	class FCatalogStateAction final : public FAIStateAction
	{
	};

	class FCatalogCondition final : public FAICondition
	{
	public:
		bool Evaluate( FAIContext& ) override
		{
			return false;
		}
	};

	class FCatalogTask final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext&, float32 ) override
		{
			return EBTStatus::Failure;
		}
	};

	class FCatalogService final : public FAIService
	{
	public:
		void Tick( FAIContext&, float32 ) override
		{
		}
	};

	//=========================================================================
	// スキーマ <-> JSON
	//=========================================================================
	NlohmannJson SchemaToJson( const FParamSchema& Schema )
	{
		NlohmannJson Params = NlohmannJson::array();
		for ( const FParamDefinition& Definition : Schema.GetParams() )
		{
			NlohmannJson Item = NlohmannJson::object();
			Item["Name"] = Definition.Name;
			Item["Type"] = FString( ToString( Definition.Type ) );
			switch ( Definition.Type )
			{
			case EParamType::Bool:
				Item["Default"] = Definition.Default.bValue;
				break;
			case EParamType::Int:
				Item["Default"] = Definition.Default.IntValue;
				break;
			case EParamType::Float:
				Item["Default"] = NJson::Round4( Definition.Default.FloatValue );
				break;
			default:
				Item["Default"] = Definition.Default.TextValue;
				break;
			}
			if ( Definition.bHasRange )
			{
				Item["Min"] = NJson::Round4( Definition.Min );
				Item["Max"] = NJson::Round4( Definition.Max );
			}
			if ( Definition.bRequired )
			{
				Item["Required"] = true;
			}
			if ( !Definition.Hint.IsEmpty() )
			{
				Item["Hint"] = Definition.Hint;
			}
			if ( Definition.Type == EParamType::Enum )
			{
				Item["Options"] = NlohmannJson::array();
				for ( const FString& Option : Definition.Options )
				{
					Item["Options"].push_back( Option );
				}
			}
			Params.push_back( Item );
		}
		return Params;
	}

	bool TryParseParamType( const FString& Text, EParamType& OutType )
	{
		for ( uint8 Index = 0; Index <= static_cast<uint8>( EParamType::Scalar ); ++Index )
		{
			const EParamType Candidate = static_cast<EParamType>( Index );
			if ( Text == ToString( Candidate ) )
			{
				OutType = Candidate;
				return true;
			}
		}
		return false;
	}

	FParamSchema SchemaFromJson( const NlohmannJson& Params, const FString& Where, TArray<FAIIssue>* Issues )
	{
		FParamSchema Schema;
		if ( !Params.is_array() )
		{
			return Schema;
		}
		int32 Index = 0;
		for ( const NlohmannJson& Item : Params )
		{
			const FString ItemWhere = FString::Printf( "%s/Params[%d]", *Where, Index++ );
			EParamType Type = EParamType::Bool;
			const FString Name = Item.is_object() ? NJson::GetOr<FString>( Item, "Name", FString() ) : FString();
			if ( Name.IsEmpty() || !TryParseParamType( NJson::GetOr<FString>( Item, "Type", FString() ), Type ) )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The parameter has no Name or an unknown Type. It was skipped." );
				continue;
			}
			const NlohmannJson* Default = Item.contains( "Default" ) ? &Item["Default"] : nullptr;
			const FString Text = ( Default != nullptr && Default->is_string() ) ? Default->get<FString>() : FString();
			switch ( Type )
			{
			case EParamType::Bool:
				Schema.Bool( *Name, Default != nullptr && Default->is_boolean() && Default->get<bool>() );
				break;
			case EParamType::Int:
				Schema.Int( *Name, ( Default != nullptr && Default->is_number() ) ? Default->get<int32>() : 0 );
				break;
			case EParamType::Float:
				Schema.Float( *Name, ( Default != nullptr && Default->is_number() ) ? Default->get<float32>() : 0.0f );
				break;
			case EParamType::String:
				Schema.String( *Name, *Text );
				break;
			case EParamType::Enum:
			{
				TArray<FString> Options;
				if ( Item.contains( "Options" ) && Item["Options"].is_array() )
				{
					for ( const NlohmannJson& Option : Item["Options"] )
					{
						if ( Option.is_string() )
						{
							Options.Add( Option.get<FString>() );
						}
					}
				}
				Schema.Enum( *Name, *FString::Join( Options, "|" ), *Text );
				break;
			}
			case EParamType::BlackboardKey:
				Schema.BlackboardKey( *Name );
				break;
			case EParamType::Clip:
				Schema.Clip( *Name );
				break;
			case EParamType::AssetPath:
				Schema.AssetPath( *Name );
				break;
			case EParamType::Scalar:
				Schema.Scalar( *Name );
				break;
			}
			if ( Item.contains( "Min" ) && Item.contains( "Max" ) && Item["Min"].is_number() && Item["Max"].is_number() )
			{
				Schema.Range( Item["Min"].get<float32>(), Item["Max"].get<float32>() );
			}
			const FString Hint = NJson::GetOr<FString>( Item, "Hint", FString() );
			if ( !Hint.IsEmpty() )
			{
				Schema.Hint( *Hint );
			}
			if ( NJson::GetOr<bool>( Item, "Required", false ) )
			{
				Schema.Required();
			}
		}
		return Schema;
	}

	template<typename TEntries>
	NlohmannJson EntriesToJson( const TEntries& Entries )
	{
		NlohmannJson List = NlohmannJson::array();
		for ( const auto& Entry : Entries )
		{
			NlohmannJson Item = NlohmannJson::object();
			Item["Name"] = Entry.Name;
			if ( !Entry.Category.IsEmpty() )
			{
				Item["Category"] = Entry.Category;
			}
			if ( !Entry.Description.IsEmpty() )
			{
				Item["Description"] = Entry.Description;
			}
			Item["Params"] = SchemaToJson( Entry.Schema );
			List.push_back( Item );
		}
		return List;
	}

	// 1 種類の部品の一覧を読んで、Register( 名前、スキーマ、カテゴリ、説明 ) で登録する（すでに Exists( 名前 ) なら、飛ばす）
	template<typename TExists, typename TRegister>
	int32 ImportList( const NlohmannJson& Catalog, const char* Key, TExists Exists, TRegister Register, TArray<FAIIssue>* Issues )
	{
		int32 Count = 0;
		const auto It = Catalog.find( Key );
		if ( It == Catalog.end() || !It->is_array() )
		{
			return 0;
		}
		int32 Index = 0;
		for ( const NlohmannJson& Item : *It )
		{
			const FString Where = FString::Printf( "%s[%d]", Key, Index++ );
			const FString Name = Item.is_object() ? NJson::GetOr<FString>( Item, "Name", FString() ) : FString();
			if ( Name.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The part has no Name. It was skipped." );
				continue;
			}
			if ( Exists( Name ) )
			{
				continue;
			}
			const FParamSchema Schema = SchemaFromJson( Item.contains( "Params" ) ? Item["Params"] : NlohmannJson::array(), Where, Issues );
			Register( Name, Schema, NJson::GetOr<FString>( Item, "Category", FString() ), NJson::GetOr<FString>( Item, "Description", FString() ) );
			++Count;
		}
		return Count;
	}
}

NlohmannJson NAI::ExportPartsCatalog( const CAIRegistry& Registry )
{
	NlohmannJson Catalog = NlohmannJson::object();
	Catalog["Version"] = 1;
	Catalog["StateActions"] = EntriesToJson( Registry.GetStateActions() );
	Catalog["Conditions"] = EntriesToJson( Registry.GetConditions() );
	Catalog["Tasks"] = EntriesToJson( Registry.GetTasks() );
	Catalog["Services"] = EntriesToJson( Registry.GetServices() );

	// 別名は、名前順に並べる（書き出すたびに、順番が変わらないように）
	TArray<FString> Olds;
	for ( const auto& Pair : Registry.GetAliases() )
	{
		Olds.Add( Pair.Key );
	}
	NAlgo::Sort( Olds.GetData(), Olds.Num() );
	Catalog["Aliases"] = NlohmannJson::array();
	for ( const FString& Old : Olds )
	{
		NlohmannJson Alias = NlohmannJson::object();
		Alias["Old"] = Old;
		Alias["Name"] = *Registry.GetAliases().Find( Old );
		Catalog["Aliases"].push_back( Alias );
	}
	return Catalog;
}

int32 NAI::ImportPartsCatalog( CAIRegistry& Registry, const NlohmannJson& Catalog, TArray<FAIIssue>* Issues )
{
	if ( !Catalog.is_object() )
	{
		AddIssue( Issues, EAIIssueSeverity::Error, FString(), "The parts catalog is not a JSON object." );
		return 0;
	}
	int32 Count = 0;
	Count += ImportList( Catalog, "StateActions", [&]( const FString& Name ) { return Registry.FindStateAction( Name ) != nullptr; },
		[&]( const FString& Name, const FParamSchema& Schema, const FString& Category, const FString& Description ) { Registry.RegisterStateAction<FCatalogStateAction>( *Name, Schema, *Category, *Description ); }, Issues );
	Count += ImportList( Catalog, "Conditions", [&]( const FString& Name ) { return Registry.FindCondition( Name ) != nullptr; },
		[&]( const FString& Name, const FParamSchema& Schema, const FString& Category, const FString& Description ) { Registry.RegisterCondition<FCatalogCondition>( *Name, Schema, *Category, *Description ); }, Issues );
	Count += ImportList( Catalog, "Tasks", [&]( const FString& Name ) { return Registry.FindTask( Name ) != nullptr; },
		[&]( const FString& Name, const FParamSchema& Schema, const FString& Category, const FString& Description ) { Registry.RegisterTask<FCatalogTask>( *Name, Schema, *Category, *Description ); }, Issues );
	Count += ImportList( Catalog, "Services", [&]( const FString& Name ) { return Registry.FindService( Name ) != nullptr; },
		[&]( const FString& Name, const FParamSchema& Schema, const FString& Category, const FString& Description ) { Registry.RegisterService<FCatalogService>( *Name, Schema, *Category, *Description ); }, Issues );

	const auto AliasesIt = Catalog.find( "Aliases" );
	if ( AliasesIt != Catalog.end() && AliasesIt->is_array() )
	{
		for ( const NlohmannJson& Item : *AliasesIt )
		{
			const FString Old = Item.is_object() ? NJson::GetOr<FString>( Item, "Old", FString() ) : FString();
			const FString Name = Item.is_object() ? NJson::GetOr<FString>( Item, "Name", FString() ) : FString();
			if ( !Old.IsEmpty() && !Name.IsEmpty() && Registry.GetAliases().Find( Old ) == nullptr )
			{
				Registry.AddAlias( *Old, *Name );
			}
		}
	}
	return Count;
}

bool NAI::SavePartsCatalog( CAssetSubsystem& Assets, const FString& RelativePath )
{
	const FString Relative = ( RelativePath.IsEmpty() ? FString( DefaultPartsCatalogPath ) : RelativePath ).Replace( "\\", "/" );
	FString Full = Assets.GetRootDirectory().Replace( "\\", "/" );
	Full += ( Full.EndsWith( "/" ) ? "" : "/" ) + Relative;
	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 )
	{
		NFileSystem::EnsureDirectory( Full.Left( Slash ) );
	}
	NJson::CJsonObject Json;
	Json.GetRoot() = ExportPartsCatalog( GetRegistry() );
	return Json.SaveToFile( Full );
}

int32 NAI::LoadPartsCatalog( CAssetSubsystem& Assets, CAIRegistry& Registry, const FString& RelativePath, TArray<FAIIssue>* Issues )
{
	const FString Relative = RelativePath.IsEmpty() ? FString( DefaultPartsCatalogPath ) : RelativePath;
	const TOptional<FString> Full = Assets.ResolvePath( Relative );
	NJson::CJsonObject Json;
	if ( !Full.IsSet() || !Json.LoadFromFile( Full.Get( FString() ) ) )
	{
		return -1;
	}
	return ImportPartsCatalog( Registry, Json.GetRoot(), Issues );
}
