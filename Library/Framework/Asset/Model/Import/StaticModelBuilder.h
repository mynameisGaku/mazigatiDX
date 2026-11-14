// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Asset/Model/Import/StaticModelDefinition.h"

// 左手系の取り込み値を、変換済みの静的な面データへ組み立てる。
class FStaticModelBuilder
{
public:
	// SceneはNImportedScene::ConvertToLeftHandedを一度適用した値。
	// 失敗時はOutを保ちErrorへ理由を返す。スキンとアニメーションは拒否する。
	static bool Build( const FImportedScene& Scene, FStaticModelDefinition& Out, FString& Error );
};
