#include "Object.h"

#include <typeinfo>

FString CObject::GetTypeName() const
{
	// MSVC の typeid 名は "class CPlayer" / "struct FFoo" の形式なので接頭辞を取り除く
	FString TypeName( typeid( *this ).name() );
	if ( TypeName.StartsWith( "class " ) ) return TypeName.RightChop( 6 );
	if ( TypeName.StartsWith( "struct " ) ) return TypeName.RightChop( 7 );
	return TypeName;
}

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CObject::InspectProperties( IPropertyInspector& Inspector )
{
}

#pragma endregion
