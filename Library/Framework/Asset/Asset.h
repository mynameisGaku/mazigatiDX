#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Object/Object.h"

//=============================================================================
// アセット（読み込んだ資源）の共通部分
//
//   アセットは CAssetManager が読み込み、TSharedPtr で渡す。同じファイルを何度読んでも 1 つを共有し、
//   最後の TSharedPtr が消えると、資源（DxLib のハンドル）も自動で解放される。
//
//   TSharedPtr<CTexture> Texture = Assets->LoadTexture( "Textures/Hero.png" );
//
//   - 読み込み・解放はメインスレッドで行う。別のスレッドで最後の参照が消えた場合は、解放を
//     メインスレッドの次の更新（CAssetManager::Tick）まで預かる
//   - アセットを保持している間は、ハンドルが有効（描画システムのラムダには、アセットではなくハンドルを渡してよいが、
//     ハンドルを使うフレームの間は、アセットを保持しておくこと）
//=============================================================================
enum class EAssetType : uint8
{
	Texture,
	SpriteSheet,
	Model,
	Sound,
	Font,
	ModelAnimation,   // モデルのアニメーションの定義（独自のデータアセット）
	World,            // ワールド（.world。置き物の配置の定義。独自のデータアセット）
	Effect,           // パーティクルのエフェクト（.effect.json。独自のデータアセット）
};
constexpr int32 NumAssetTypes = 8;

const char* ToString( EAssetType Type );

class CAsset : public CObject
{
public:
	~CAsset() override = default;

	FORCEINLINE EAssetType GetAssetType() const { return Type; }
	// 読み込んだときのパス（正規化したフルパス。コードから作ったものは、付けた名前）
	FORCEINLINE const FString& GetPath() const { return Path; }

	// 読み込みが終わっていて、使える状態か。同期の読み込みなら、成功していれば常に true
	virtual bool IsReady() const { return true; }

	// デバッグ表示用の一行説明（大きさ・長さなど）
	virtual FString Describe() const { return FString(); }

	// 使っているメモリの推定（バイト）。画像は 幅 x 高さ x 4、モデルは頂点とテクスチャ、サウンドは展開後の大きさ、など。
	// ほかのアセットが持っているもの（スプライトシートの画像など）は含めない。プロファイラーの Counters と Assets 画面に出る
	virtual int64 GetMemoryBytes() const { return 0; }
	// ファイルの大きさ（バイト。コードから作ったものは 0）
	FORCEINLINE int64 GetFileBytes() const { return FileBytes; }
	// 読み込みにかかった時間（ミリ秒。非同期の読み込みは、始めるまでの時間だけ）
	FORCEINLINE float32 GetLoadMs() const { return LoadMs; }
	// CAssetManager が、読み込んだ直後に記録する
	void SetLoadInfo( int64 InFileBytes, float32 InLoadMs )
	{
		FileBytes = InFileBytes;
		LoadMs = InLoadMs;
	}

protected:
	CAsset( EAssetType InType, const FString& InPath ) : Type( InType ), Path( InPath ) {}

private:
	EAssetType Type;
	FString Path;
	int64 FileBytes = 0;
	float32 LoadMs = 0.0f;
};

//=============================================================================
// NAssetService
//   アセットの実装（CTexture など）が、バックエンドを使うための窓口。CAssetManager が動いている間だけ有効。
//   ゲーム側が直接使うものではない。
//=============================================================================
namespace NAssetService
{
	// 動いていなければ nullptr
	IAssetBackend* GetBackend();

	// 資源を解放する。メインスレッドならすぐに、そうでなければ次の更新で
	void ReleaseTexture( int32 Handle );
	void ReleaseModel( int32 Handle );
	void ReleaseModelMaterialGeometry( int32 Geometry );
	void ReleaseFont( int32 Handle );
	void ReleaseSound( int32 Handle );
	void ReleaseVertexBuffer( int32 Handle );
	void ReleaseIndexBuffer( int32 Handle );
	void ReleaseShadowMap( int32 Handle );

	// フォントファイルの利用（AddFontFile）を数えながら行う。最後の利用者が RemoveFontFile したときに実際に外す
	bool AddFontFile( const FString& Path );
	void RemoveFontFile( const FString& Path );

	bool IsMainThread();
}
