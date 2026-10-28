#include "SampleCatalog.h"

bool CSampleCatalog::IsValidName( const FString& Name )
{
	if ( Name.IsEmpty() ) return false;

	for ( int32 i = 0; i < Name.Len(); ++i )
	{
		const char Ch = Name[i];
		const bool bLetter = ( Ch >= 'A' && Ch <= 'Z' ) || ( Ch >= 'a' && Ch <= 'z' );
		const bool bDigitOrUnderscore = ( Ch >= '0' && Ch <= '9' ) || Ch == '_';
		if ( !bLetter && !( i > 0 && bDigitOrUnderscore ) ) return false;
	}
	return true;
}

CSampleCatalog& CSampleCatalog::Get()
{
	// 関数の中の static にして、ほかの .cpp の静的な初期化から呼ばれても、使う前に作られているようにする
	static CSampleCatalog Instance;
	return Instance;
}

bool CSampleCatalog::AddEntry( const FSampleInfo& Entry )
{
	if ( !IsValidName( Entry.Name ) )
	{
		Errors.Add( FString::Printf( "The sample name '%s' is not valid (it must start with a letter and contain only letters, digits and '_').", *Entry.Name ) );
		return false;
	}
	if ( Entry.Name == GetBrowserSceneName() )
	{
		Errors.Add( FString::Printf( "The sample name '%s' is reserved for the list screen.", *Entry.Name ) );
		return false;
	}
	if ( Find( Entry.Name ) != nullptr )
	{
		Errors.Add( FString::Printf( "The sample name '%s' is already used by another sample.", *Entry.Name ) );
		return false;
	}

	Samples.Add( Entry );
	return true;
}

TArray<FSampleInfo> CSampleCatalog::GetSorted() const
{
	TArray<FSampleInfo> Sorted = Samples;
	Sorted.Sort( []( const FSampleInfo& A, const FSampleInfo& B )
				 {
					 if ( A.Order != B.Order ) return A.Order < B.Order;
					 return A.Name < B.Name;
				 } );
	return Sorted;
}

const FSampleInfo* CSampleCatalog::Find( const FString& Name ) const
{
	for ( const FSampleInfo& Sample : Samples )
	{
		if ( Sample.Name == Name ) return &Sample;
	}
	return nullptr;
}

void CSampleCatalog::RegisterAll( CSceneManager& Scenes ) const
{
	for ( const FSampleInfo& Sample : GetSorted() )
	{
		Sample.RegisterScene( Scenes, Sample.Name );
	}
}
