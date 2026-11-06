// SPDX-License-Identifier: MIT
#include "WorldAssetUse.h"

TArray<FWorldAssetUse> NWorld::FindAssetUses( const FWorldDefinition& Definition, const FString& AssetPath )
{
	TArray<FWorldAssetUse> Uses;
	if ( AssetPath.IsEmpty() )
	{
		return Uses;
	}
	const FString Target = NormalizeAssetPath( AssetPath );
	for ( const FWorldObject& Object : Definition.Objects )
	{
		const auto Check = [&]( const FString& Path, const char* Component )
		{
			if ( !Path.IsEmpty() && NormalizeAssetPath( Path ) == Target )
			{
				FWorldAssetUse Use;
				Use.ObjectId = Object.Id;
				Use.ObjectName = Object.Name;
				Use.Component = Component;
				Uses.Add( Use );
			}
		};
		if ( Object.Model.IsSet() )
		{
			Check( Object.Model.GetValue().Path, "Model" );
			Check( Object.Model.GetValue().Animations, "Model animations" );
		}
		if ( Object.Billboard.IsSet() )
		{
			Check( Object.Billboard.GetValue().Texture, "Billboard" );
		}
		if ( Object.Sound.IsSet() )
		{
			Check( Object.Sound.GetValue().Path, "Sound" );
		}
		if ( Object.Effect.IsSet() )
		{
			Check( Object.Effect.GetValue().Path, "Effect" );
		}
		if ( Object.Collider.IsSet() )
		{
			Check( Object.Collider.GetValue().Mesh, "Collider mesh" );
		}
		if ( Object.Prefab.IsSet() )
		{
			Check( Object.Prefab.GetValue().Path, "Prefab" );
		}
	}
	return Uses;
}
