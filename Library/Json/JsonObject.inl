#pragma once

// JsonObject.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// JsonObject.h の末尾から include される。直接 include しない

template<typename T>
T NJson::CJsonObject::GetValue( const FString& Key, const T& Default ) const
{
	const NlohmannJson* Node = FindNode( Key );
	if ( Node == nullptr || Node->is_null() )
	{
		NotifyMissingKey( Key );
		return Default;
	}
	try
	{
		return Node->get<T>();
	}
	catch ( const NlohmannJson::exception& Exception )
	{
		NotifyInvalidValue( Key, Exception.what() );
		return Default;
	}
}