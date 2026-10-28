#pragma once
#include "Library/Framework/Asset/Asset.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Memory/SharedPtr.h"

//=============================================================================
// CTexture
//   画像。CAssetManager::LoadTexture で読み込む。描画の窓口（FDrawContext）には、IImage として渡せる。
//
//   TSharedPtr<CTexture> Texture = Assets->LoadTexture( "Textures/Hero.png" );
//   Draw.Image( X, Y, Texture );                               // 描画システムの中でも、FImmediateDraw でも、そのまま渡せる（nullptr なら何も描かない）
//   DrawGraph( 0, 0, Texture->GetHandle(), TRUE );             // 素の DxLib へは、ハンドル（int）を渡す
//
//   非同期で読み込んだ場合、IsReady() が true になるまで大きさは 0（描くと何も出ない）。
//=============================================================================
class CTexture : public CAsset, public IImage
{
public:
	CTexture( const FString& InPath, int32 InHandle, bool bInAsync );
	// 派生画像用。親を保持し、指定寸法を読み込み待ちなしで返す。
	CTexture( const FString& InPath, int32 InHandle, const TSharedPtr<CTexture>& InParent, int32 InWidth, int32 InHeight, const float32 ( &InPolygonUvBounds )[4] );
	// 派生画像のpolygon描画で使うbase texture上のUV範囲。
	FORCEINLINE const float32 ( &GetPolygonUvBounds() const )[4] { return PolygonUvBounds; }
	~CTexture() override;

	FORCEINLINE int32 GetHandle() const
	{
		return Handle;
	}

	// 描画の窓口に渡すハンドル（IImage）。作られていない・読み込めなかった画像は無効
	FImageHandle GetImageHandle() const override
	{
		return FImageHandle( Handle );
	}

	// 画像の大きさ（ピクセル）。読み込み中は 0
	int32 GetWidth() const;
	int32 GetHeight() const;

	bool IsReady() const override;
	FString Describe() const override;
	// 幅 x 高さ x 4（RGBA 8bit）。ミップマップなどは含めない
	int64 GetMemoryBytes() const override;

private:
	// 大きさが分かるまで、問い合わせる
	void RefreshSize() const;

	int32 Handle;
	bool bAsync;
	// 派生元を子ハンドルより長く保持する。
	TSharedPtr<CTexture> ParentTexture;
	// DxLib通常Quadが派生グラフを自動補正しない場合に使うUV。
	float32 PolygonUvBounds[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	mutable int32 Width = 0;
	mutable int32 Height = 0;
};
