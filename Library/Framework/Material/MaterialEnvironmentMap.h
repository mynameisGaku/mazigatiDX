// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/Material/MaterialSubstrate.h"

class CAssetSubsystem;

//=============================================================================
// CMaterialEnvironmentMap: マテリアルに映り込む、まわりの景色（環境マップ）
//   横長の全周の絵（正距円筒図法。幅が高さの 2 倍。上端が真上、絵の中央が +Z の向き）から作る。光を計算する Surface のマテリアル（DefaultLit・Substrate）が、
//   鏡面の映り込みと、まわりからの光（拡散）に使う。粗い面ほど、ぼけた映り込みになる（作るときに、粗さごとにぼかした絵を用意する。FSubstrateEnvironment）。
//
//   TSharedPtr<CMaterialEnvironmentMap> Environment = CMaterialEnvironmentMap::Load( *Assets, "Engine:/Environments/Sky.png" );
//   FMaterialSceneState Scene;
//   Scene.EnvironmentMap = Environment;        // 無ければ（null）、空と地面の色（SkyColor・GroundColor・AmbientColor）
//   Scene.EnvironmentIntensity = 1.0f;
//   Material->BeginSurface( Draw, Scene );
//
//   - 作るのは、メインスレッドだけ（画像を作る）。ぼかす計算をするので、読み込みのときに 1 回だけ作ること（毎フレーム作らない）。
//   - 色は、0〜1（8 ビット）。明るい空は、EnvironmentIntensity で持ち上げる。
//=============================================================================
class CMaterialEnvironmentMap
{
public:
	// 全周の絵から作る。絵が無効・画像を作れないなら null
	static TSharedPtr<CMaterialEnvironmentMap> Create( CAssetSubsystem& Assets, const FString& Name, const FPixelImage& Panorama );
	// 画像のファイルから作る（CAssetSubsystem::LoadPixelImage）。読めなければ null
	static TSharedPtr<CMaterialEnvironmentMap> Load( CAssetSubsystem& Assets, const FString& Path );

	// CPU 側の絵（シェーダーと同じ計算の Sample を持つ。検査や、CPU での陰影の計算に使う）
	const FSubstrateEnvironment& GetEnvironment() const { return Environment; }
	// GPU 側の画像（段を縦に並べたもの）
	const TSharedPtr<CDynamicTexture>& GetTexture() const { return Texture; }

private:
	CMaterialEnvironmentMap() = default;

	FSubstrateEnvironment Environment;
	TSharedPtr<CDynamicTexture> Texture;
};
