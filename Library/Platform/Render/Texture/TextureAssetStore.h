// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Collection/Map.h"

// アセット番号と画像資源番号の対応を所有する。codec・GPU・ImGuiを知らない。
class CTextureAssetStore : public ITextureDrawSource
{
public:
	// 未接続で作る。Initializeまで作成・参照は失敗する。
	CTextureAssetStore() = default;
	// Resourcesは、Shutdownまで生存させる。
	explicit CTextureAssetStore( IRenderTextureResources& InResources );
	// 登録画像の解放要求を資源の所有者へ戻す。
	~CTextureAssetStore() override;
	// 登録番号と所有画像は複写しない。
	CTextureAssetStore( const CTextureAssetStore& ) = delete;
	CTextureAssetStore& operator=( const CTextureAssetStore& ) = delete;
	// 未接続の場合だけ資源の所有先を固定する。既存の登録と番号を保つ。
	bool Initialize( IRenderTextureResources& InResources );
	// 全登録を外して接続を切る。再初期化しても番号は再利用しない。
	void Shutdown();
	// RGBA8を複写して登録する。失敗時は-1。番号を再利用しない。
	int32 Create( int32 Width, int32 Height, const FColor32* Pixels );
	// 同じ寸法の画像を更新する。失敗時は内容を保つ。
	bool Update( int32 Handle, int32 Width, int32 Height, const FColor32* Pixels );
	// 登録を外す。GPU完了待ちと実体の解放は資源の所有者が行う。
	void Delete( int32 Handle );
	// すべて登録から外す。次の番号は巻き戻さない。
	void Reset();
	// 有効な登録数。UIのフォント等、別の所有者の画像は数えない。
	int32 Num() const;
	// 有効な画像の寸法。失敗時は両出力を保つ。
	bool GetSize( int32 Handle, int32& OutWidth, int32& OutHeight ) const;
	// 自分の登録番号だけを資源のviewへ解決する。失敗時はOutを保つ。
	bool GetTextureDrawView( int32 Handle, FTextureDrawView& Out ) override;
private:
	IRenderTextureResources* Resources = nullptr; // GPU資源の作成・更新・寿命の窓口。
	TMap<int32, FTextureResourceHandle> Textures; // 異なる名前空間の番号を対応させる。
	int32 NextHandle = 0; // 解放後も使い回さないアセット番号。
	static constexpr int32 MaxHandles = 1024; // この窓口で同時に登録する上限。
};
