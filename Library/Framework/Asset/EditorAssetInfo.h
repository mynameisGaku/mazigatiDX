// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Framework/World/Editor/EditorShapes.h"

class CAssetManager;
class FStaticModelSurface;

//=============================================================================
// FAssetEditorInfo
//   ワールドエディタ（EditorShapes.h の IEditorAssetInfo）の、CAssetManager による実装。
//   モデルの大きさ・画像の大きさ・ファイルの有無を答える。読み込んだモデル・画像は、画面に出す側（エディタのビューポート）にも渡す。
//
//   ピッキングと描画が毎フレーム何度も問い合わせるので、読み込み（失敗も）をパスごとに覚える。
//   失敗した読み込みは、ログに 1 回だけ出る（毎フレームは出さない）。ファイルを差し替えたら Clear() で覚えたものを捨てる。
//   メインスレッド専用。
//=============================================================================
class FAssetEditorInfo final : public IEditorAssetInfo
{
public:
	explicit FAssetEditorInfo( CAssetManager& InAssets );

	bool Exists( const FString& Path ) const override;
	bool GetModelBounds( const FString& Path, FVector3& OutMin, FVector3& OutMax ) const override;
	bool GetTextureSize( const FString& Path, int32& OutWidth, int32& OutHeight ) const override;
	const FEditorCollisionMesh* GetCollisionMesh( const FString& Path ) const override;
	// 入れ子のワールド（Prefab 部品）の中身。読み込めなければ nullptr（失敗も覚える）
	const FWorldDefinition* GetWorld( const FString& Path ) const override;

	// 画面に出すための、読み込み済みのモデル（自分用のインスタンス。同じインスタンスを、行列を変えて何度でも描ける）と画像。読み込めなければ nullptr
	TSharedPtr<CModelInstance> GetModelInstance( const FString& Path ) const;
	// メインスレッドでsourceを取得する。未readyでも返し、読み込み失敗・無効pathでは空を返す。
	TSharedPtr<CModel> GetModelSource( const FString& Path ) const;
	// メインスレッド専用。未準備なら空を返し、ready後に静的面snapshotを一度だけ試す。失敗もcacheする。
	TSharedPtr<const FStaticModelSurface> PrepareModelSurface( const FString& Path ) const;
	// 既存cacheの準備済みsnapshotだけを返す。読み込み・準備・状態変更は行わない。
	TSharedPtr<const FStaticModelSurface> FindPreparedModelSurface( const FString& Path ) const;
	TSharedPtr<CTexture> GetTexture( const FString& Path ) const;

	// 覚えたものをすべて捨てる（次の問い合わせで読み直す）
	void Clear();

private:
	struct FModelEntry
	{
		TSharedPtr<CModel> Model;
		TSharedPtr<CModelInstance> Instance;
		mutable TSharedPtr<const FStaticModelSurface> SurfaceSnapshot; // 元モデルの寿命と借用画像handleを保つ不変snapshot
		mutable bool bSurfaceSnapshotAttempted = false; // ready後の成功・恒久失敗を再試行しない印
	};

	const FModelEntry& FindModel( const FString& Path ) const;

	CAssetManager& Assets;
	FAssetWorldQuery Query;
	mutable TMap<FString, FModelEntry> Models;
	mutable TMap<FString, TSharedPtr<CTexture>> Textures;   // 読み込めなかったものは、空のポインタで覚える
	mutable TMap<FString, TSharedPtr<FEditorCollisionMesh>> Meshes;   // 同じく（取れなかったものは、空のポインタ）
	mutable TMap<FString, TSharedPtr<CWorldAsset>> Worlds;            // 入れ子のワールド。同じく
};
