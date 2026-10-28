#include "JsonObject.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Utility.h"

DEFINE_LOG_CATEGORY_STATIC( LogJson, Log )

namespace
{
	// "A.B.C" -> { "A", "B", "C" }（nlohmann のキーの型にしておく）
	TArray<NJson::FJsonString> SplitKeyPath( const FString& Key )
	{
		TArray<NJson::FJsonString> Path;
		for ( const FString& Part : Key.ParseIntoArray( "." ) )
		{
			Path.Add( NJson::ToJsonString( Part ) );
		}
		return Path;
	}
}

bool NJson::CJsonObject::LoadFromFile( const FString& Filename )
{
	bLoaded = false;

	// テキストとして全部読む（CRLF は LF になる）
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Filename, Bytes, true ) )
	{
		LOG( LogJson, Error, "Failed to open file: %s", *Filename );
		return false;
	}

	// UTF-8 の BOM (EF BB BF) は読み飛ばす（エディターが付けることがある）
	const uint8* Begin = Bytes.GetData();
	const uint8* const End = Begin + Bytes.Num();
	if ( Bytes.Num() >= 3 && Begin[0] == 0xEF && Begin[1] == 0xBB && Begin[2] == 0xBF ) Begin += 3;

	// 第 3 引数 false: 構文エラーで例外を投げず discarded を返す
	NlohmannJson Parsed = NlohmannJson::parse( Begin, End, nullptr, false );
	if ( Parsed.is_discarded() )
	{
		LOG( LogJson, Error, "Failed to parse JSON: %s", *Filename );
		return false;
	}
	if ( !Parsed.is_object() )
	{
		LOG( LogJson, Error, "Root of JSON must be an object: %s", *Filename );
		return false;
	}

	Root = MoveTemp( Parsed );
	bLoaded = true;
	LOG( LogJson, Verbose, "Loaded %s (%d top-level key(s))", *Filename, static_cast<int32>( Root.size() ) );
	return true;
}

bool NJson::CJsonObject::SaveToFile( const FString& Filename, int32 Indent ) const
{
	// テキストとして書く（LF は CRLF になる）
	FFile File;
	if ( !File.Open( Filename, FFile::EMode::Write, true ) )
	{
		LOG( LogJson, Error, "Failed to open file for writing: %s", *Filename );
		return false;
	}

	const NJson::FJsonString Text = Root.dump( Indent ) + '\n';
	const bool bWritten = File.Write( Text.data(), Text.size() ) == Text.size();
	if ( !File.Close() || !bWritten )
	{
		LOG( LogJson, Error, "Failed while writing %s", *Filename );
		return false;
	}
	LOG( LogJson, Verbose, "Saved %s", *Filename );
	return true;
}

void NJson::CJsonObject::Set( const FString& Key, const NlohmannJson& Value )
{
	const TArray<NJson::FJsonString> Path = SplitKeyPath( Key );
	if ( Path.Num() == 0 ) return;

	// 途中のオブジェクトが無ければ作る（オブジェクト以外なら置き換える）
	NlohmannJson* Node = &Root;
	for ( int32 i = 0; i < Path.Num() - 1; ++i )
	{
		NlohmannJson& Child = ( *Node )[Path[i]];
		if ( !Child.is_object() ) Child = NlohmannJson::object();
		Node = &Child;
	}
	( *Node )[Path.Last()] = Value;
}

NlohmannJson NJson::CJsonObject::Get( const FString& Key ) const
{
	const NlohmannJson* Node = FindNode( Key );
	return Node != nullptr ? *Node : NlohmannJson();
}

bool NJson::CJsonObject::HasKey( const FString& Key ) const
{
	return FindNode( Key ) != nullptr;
}

bool NJson::CJsonObject::Remove( const FString& Key )
{
	FString ParentKey;
	FString ChildKey;
	if ( !Key.Split( ".", &ParentKey, &ChildKey, ESearchCase::CaseSensitive, true ) )
	{
		const bool bRemoved = Root.erase( NJson::ToJsonString( Key ) ) > 0;
		LOG( LogJson, Verbose, "Remove '%s': %s", *Key, bRemoved ? "removed" : "key not found" );
		return bRemoved;
	}

	const NlohmannJson* Parent = FindNode( ParentKey );
	if ( Parent == nullptr || !Parent->is_object() )
	{
		LOG( LogJson, Verbose, "Remove '%s': parent '%s' not found", *Key, *ParentKey );
		return false;
	}
	const bool bRemoved = const_cast<NlohmannJson*>( Parent )->erase( NJson::ToJsonString( ChildKey ) ) > 0;
	LOG( LogJson, Verbose, "Remove '%s': %s", *Key, bRemoved ? "removed" : "key not found" );
	return bRemoved;
}

void NJson::CJsonObject::NotifyMissingKey( const FString& Key ) const
{
	// 省略可能な項目で呼ばれることが多いので Verbose（既定値が使われる）
	LOG( LogJson, Verbose, "Key '%s' not found (or null). Using the default value.", *Key );
}

void NJson::CJsonObject::NotifyInvalidValue( const FString& Key, const char* Reason ) const
{
	LOG( LogJson, Warning, "Key '%s' has an unexpected type: %s. Using the default value.", *Key, Reason );
}

void NJson::CJsonObject::Clear()
{
	Root = NlohmannJson::object();
	bLoaded = false;
}

usize NJson::CJsonObject::Num() const
{
	return Root.size();
}

TArray<FString> NJson::CJsonObject::GetKeys() const
{
	TArray<FString> Keys;
	for ( auto It = Root.begin(); It != Root.end(); ++It )
	{
		Keys.Add( NJson::FromJsonString( It.key() ) );
	}
	return Keys;
}

const NlohmannJson* NJson::CJsonObject::FindNode( const FString& Key ) const
{
	const TArray<NJson::FJsonString> Path = SplitKeyPath( Key );
	if ( Path.Num() == 0 ) return nullptr;

	const NlohmannJson* Node = &Root;
	for ( const NJson::FJsonString& Part : Path )
	{
		if ( !Node->is_object() ) return nullptr;
		const auto It = Node->find( Part );
		if ( It == Node->end() ) return nullptr;
		Node = &*It;
	}
	return Node;
}
