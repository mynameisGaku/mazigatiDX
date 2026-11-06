// SPDX-License-Identifier: MIT
#include "Asset.h"

const char* ToString( EAssetType Type )
{
	switch ( Type )
	{
	case EAssetType::Texture: return "Texture";
	case EAssetType::SpriteSheet: return "SpriteSheet";
	case EAssetType::Model: return "Model";
	case EAssetType::Sound: return "Sound";
	case EAssetType::Font: return "Font";
	case EAssetType::ModelAnimation: return "ModelAnimation";
	case EAssetType::World: return "World";
	case EAssetType::Effect: return "Effect";
	case EAssetType::Blackboard: return "Blackboard";
	case EAssetType::StateMachine: return "StateMachine";
	}
	return "?";
}
