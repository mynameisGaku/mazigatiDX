#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// IPropertyInspector
//   オブジェクトが自分のプロパティを列挙するための訪問者。
//   オブジェクト側は UI ライブラリを知らずに済み、表示方法（ImGui / ログ出力など）は実装側が決める。
//
//   void CEnemy::InspectProperties( IPropertyInspector& Inspector )
//   {
//       CGameObject::InspectProperties( Inspector );
//       Inspector.Int( "Hp", Hp );          // 編集されたら true が返る
//   }
//=============================================================================
class IPropertyInspector
{
public:
	virtual ~IPropertyInspector() = default;

	// 読み取り専用
	virtual void Text( const char* Label, const FString& Value ) = 0;

	// 編集可能。値が変更されたら true
	virtual bool Bool( const char* Label, bool& Value ) = 0;
	virtual bool Int( const char* Label, int32& Value ) = 0;
	virtual bool Float( const char* Label, float32& Value ) = 0;
	virtual bool Vector3( const char* Label, FVector3& Value ) = 0;
};
