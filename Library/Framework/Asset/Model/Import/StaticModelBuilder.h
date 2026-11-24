// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Asset/Model/Import/StaticModelDefinition.h"

// 左手系の取り込み値を、変換済みの静的な面データへ組み立てる。
class FStaticModelBuilder
{
public:
	// SceneはNImportedScene::ConvertToLeftHandedを一度適用した値。
	// 失敗時はOutを保ちErrorへ理由を返す。bAllowRigがfalseなら、スキンとアニメーションは拒否する。
	// bAllowRigがtrueなら、スキンつきのメッシュ（基準の姿の頂点のまま）と、アニメーションのトラックと、メッシュの無いシーン（アニメーションだけ）を受け付ける。
	// OutSourcesは、Out.Surface.Verticesと同じ並びで、各頂点の元のメッシュと頂点の番号を返す（骨組みの変形に使う）。
	static bool Build( const FImportedScene& Scene, FStaticModelDefinition& Out, FString& Error, bool bAllowRig = false, TArray<FModelVertexSource>* OutSources = nullptr );
	// 親を含む鏡映変換では頂点順を補正し、表の向きと法線を保つ。
};
