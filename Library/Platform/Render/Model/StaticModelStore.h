// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Interfaces/Asset/TextureBackend.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Framework/Asset/Model/Import/StaticModelDefinition.h"

// 不変形状と画像を共有し、個体の状態と登録番号を所有する。GPUに依存しない。
class FStaticModelStore : public IStaticModelSource
{
public:
	// 画像の解放先の寿命を変えないよう、所有者を複写しない。
	FStaticModelStore() = default;
	FStaticModelStore( const FStaticModelStore& ) = delete;
	FStaticModelStore& operator=( const FStaticModelStore& ) = delete;
	// 左手系のSceneを登録する。Texturesは全個体の解放まで生存させる。
	// 失敗時は-1と理由を返し、読み込み途中の画像を残さない。
	int32 Create( const FImportedScene& Scene, const FString& SourcePath, ITextureBackend& Textures, FString& Error );
	// 形状と画像を共有し、不透明度を値でコピーした個体を作る。
	int32 Duplicate( int32 Source );
	// 最後の共有個体を削除したとき画像も解放する。
	void Delete( int32 Handle );
	// 全個体を解放する。登録番号は再利用しない。
	void Reset();
	// 登録中の個体数。
	int32 Num() const;
	// 同期の命令解釈中だけ不変形状を貸す。
	bool GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out ) override;
	// 借用画像番号を含む面データをコピーする。失敗時は出力を保つ。
	bool GetSurface( int32 Handle, FStaticModelSurfaceData& Out ) const;
	// 準備済みの不変定義。登録の追加・変更・削除前に借用を終える。
	const FStaticModelDefinition* GetDefinition( int32 Handle ) const;
	// 有効な個体の不透明度を返す。
	bool GetOpacity( int32 Handle, float32& Out ) const;
	// 0〜1の有限値だけを設定する。失敗時は元の値を保つ。
	bool SetOpacity( int32 Handle, float32 Opacity );
	// 個体が共有する形状のメモリ推定。画像は含めない。
	int64 EstimateBytes( int32 Handle ) const;
private:
	// 形状は登録時に完成し、以降は書き換えない。
	struct FResource
	{
		// 途中の読み込み失敗でも、取得済みの画像を戻す。
		~FResource();
		FStaticModelDefinition Definition; // 面データとフレーム情報。
		TArray<FDrawVertex3D> Vertices; // 共通描画向けの頂点。
		TArray<FStaticModelDrawSection> Sections; // 共通描画向けの材質範囲。
		TArray<int32> OwnedTextures; // 最後の個体まで所有する画像。
		ITextureBackend* TextureBackend = nullptr; // 所有画像の解放先。
		int64 Bytes = 0; // 形状のメモリ推定。
	};
	// 形状の共有と個体別の値を分ける。
	struct FInstance
	{
		TSharedPtr<FResource> Resource; // 個体間で共有する不変形状。
		float32 Opacity = 1; // この個体だけの不透明度。
	};
	TMap<int32, FInstance> Instances; // 登録番号から個体への表。
	int32 NextHandle = 0; // 解放後も再利用しない登録番号。
};
