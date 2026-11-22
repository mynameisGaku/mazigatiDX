// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Render/TextureDrawView.h"
#include "Library/Core/Render/TextureResourceHandle.h"

// 画像の描画 view（描画実装の識別子と寸法）を、ゲームスレッドで覚えておく。
// 描画スレッドが持つ画像の登録表への問い合わせを、画像 1 枚を描くたびの往復にしないための置き場。
// 覚えるのは、成功した問い合わせだけ。画像の作成・更新・解放、撮影の切り替えなど、問い合わせの答えを変えうる操作のたびに、全部を捨てる。
// 描画先のパスの始まりは、そのパスが書く描画先と深度の分だけを捨てる（Remove。ほかの画像の答えは、パスで変わらない）。フレームの境目・パスの終わりは、捨てない。
// ゲームスレッドだけで使う。
class CTextureViewCache
{
  public:
	// 覚えている view を取り出す。なければ false（出力は変えない）。
	bool Find( FTextureResourceHandle Texture, FTextureDrawView &OutView ) const;
	// 成功した問い合わせの答えを覚える。上限に達していたら、全部を捨ててから覚える。
	void Add( FTextureResourceHandle Texture, const FTextureDrawView &View );
	// 1 枚の画像の view だけを捨てる（描画先のパスが書く間は、その画像を view として貸さないため）。覚えていなければ、何もしない。
	void Remove( FTextureResourceHandle Texture );
	// 覚えている view を、全部捨てる。
	void Invalidate();
	// 覚えている view の数。
	int32 GetNumViews() const { return Views.Num(); }

  private:
	static constexpr int32 MaxViews = 4096; // 画像が際限なく増えても、覚える量を抑える。
	TMap<uint64, FTextureDrawView> Views; // 画像の番号から、描画の view への表。
};
