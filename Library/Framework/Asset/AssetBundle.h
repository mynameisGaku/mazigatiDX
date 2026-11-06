// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Asset.h"

//=============================================================================
// FAssetBundle
//   アセットをまとめて持っておく入れ物（シーンの OnEnter で読み込んで持ち、シーンの終わりで手放す、など）。
//   持っている間は、使っていなくても解放されない。
//
//   FAssetBundle Bundle;
//   Bundle.Add( Assets->LoadTexture( "Textures/Hero.png" ) );
//   Bundle.Add( Assets->LoadSound( "Sound/Jump.wav" ) );
//   ...
//   Bundle.Clear();     // 他で使われていなければ、ここで解放される
//=============================================================================
class FAssetBundle
{
public:
	template<typename T>
	const TSharedPtr<T>& Add( const TSharedPtr<T>& Asset )
	{
		if ( Asset.IsValid() )
		{
			Assets.Add( TSharedPtr<CAsset>( Asset ) );
		}
		return Asset;
	}

	void Clear()
	{
		Assets.Empty();
	}

	int32 Num() const
	{
		return Assets.Num();
	}

	// すべて使える状態になったか（非同期の読み込みの完了待ちに）
	bool AreAllReady() const;

private:
	TArray<TSharedPtr<CAsset>> Assets;
};
