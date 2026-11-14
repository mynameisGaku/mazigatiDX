#pragma once
#include "WorldDefinition.h"
#include "Library/Json/JsonHelpers.h"

//=============================================================================
// ワールドの定義と JSON の変換の部品（World/Definition の中だけで使う）
//   書式は WorldDefinition.cpp の先頭。FWorldDefinition::ToJson / FromJson が、ここの関数を組み合わせる。
//   共通の補助とオブジェクトは WorldDefinitionJson.cpp、部品（Model / Billboard など）は WorldComponentJson.cpp。
//=============================================================================
namespace NWorldDefinitionJson
{
	inline constexpr const char* KnownWorldKeys[] = { "Version", "Name", "NextId", "Settings", "Objects" };
	inline constexpr const char* KnownObjectKeys[] = { "Id", "Name", "Parent", "Enabled", "Tags", "Props", "Transform", "Collision", "Components" };
	inline constexpr const char* KnownComponentTypes[] = { "Model", "Billboard", "Sound", "Effect", "Collider", "Mesh", "Prefab" };

	template<size_t N>
	bool IsKnownKey( const char* const ( &Keys )[N], const NJson::FJsonString& Key )
	{
		for ( const char* Known : Keys )
		{
			if ( Key == Known )
			{
				return true;
			}
		}
		return false;
	}

	//-------------------------------------------------------------------------
	// 共通の補助
	//-------------------------------------------------------------------------
	void Warn( TArray<FString>* Warnings, const FString& Message );
	float64 Round6( float64 Value );
	bool IsNearlyEqual( const FVector3& A, const FVector3& B );
	NlohmannJson Vector2ToJson( const FVector2& V );
	FVector2 ReadVector2( const NlohmannJson& Object, const char* Key, const FVector2& Default );
	bool EqualsIgnoreCase( const FString& A, const char* B );
	// 非 null の数の配列か
	bool IsNumberArray( const NlohmannJson& Json, size_t Count );

	//-------------------------------------------------------------------------
	// 部品
	//-------------------------------------------------------------------------
	NlohmannJson ModelToJson( const FWorldModel& Model );
	FWorldModel ModelFromJson( const NlohmannJson& Json );
	NlohmannJson PrefabToJson( const FWorldPrefab& Prefab );
	FWorldPrefab PrefabFromJson( const NlohmannJson& Json );
	NlohmannJson BillboardToJson( const FWorldBillboard& Board );
	FWorldBillboard BillboardFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where );
	NlohmannJson SoundToJson( const FWorldSound& Sound );
	NlohmannJson EffectToJson( const FWorldEffect& Effect );
	FWorldEffect EffectFromJson( const NlohmannJson& Json );
	FWorldSound SoundFromJson( const NlohmannJson& Json );
	NlohmannJson ColliderToJson( const FWorldCollider& Collider );
	FWorldCollider ColliderFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where );

	//-------------------------------------------------------------------------
	// オブジェクト
	//-------------------------------------------------------------------------
	NlohmannJson TransformToJson( const FWorldTransform& Transform );
	FWorldTransform TransformFromJson( const NlohmannJson& Json );
	NlohmannJson ObjectToJson( const FWorldObject& Object );
	FWorldObject ObjectFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, int32 Index );
}
