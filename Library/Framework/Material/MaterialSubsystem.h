// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Material/MaterialInstance.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CMaterialSubsystem
//   マテリアルの、共通の持ち物: 時間・コレクション（MPC）の名前の表・マテリアルのファイル（.gxmaterial / .gxmaterialinstance / .gxmaterialparametercollection）の読み込みとキャッシュ・ホットリロード。
//   GetAppSubsystem<CMaterialSubsystem>() で取る。
//
//   CMaterialSubsystem* Materials = GetAppSubsystem<CMaterialSubsystem>();
//   TSharedPtr<CMaterial> Glow = Materials->LoadMaterial( "Engine:/Materials/UI/GlowButton.gxmaterial" );
//   Materials->GetParameterCollection( "Weather" )->SetScalar( "Rain", 0.8f );   // "Weather" を読むすべてのマテリアルに反映される
//
//   - パスは、ほかのアセットと同じ（"Materials/Foo.gxmaterial"・"Engine:/Materials/..."）。読み込んだマテリアルは、パスでキャッシュされる（同じパスは、同じ物）。
//     画像（TextureSample の既定の画像）も、いっしょに読み込む。読めない・壊れているファイルは、ログを出して null（診断は、マテリアルの GetDiagnostics）。
//   - .gxmaterialinstance は、親のマテリアル（これも、パスで読み込む）と上書きの値から、インスタンスを作る。
//   - ホットリロード: SetHotReloadInterval（秒。既定 0.5。0 以下で止める）ごとに、読み込んだファイルの中身が変わっていないか調べ、変わっていれば、同じ CMaterial を作り直す
//     （マテリアルを使っているブラシなどは、そのまま使える）。壊れた中身へ書き換えられたら、古い見た目のまま（診断に理由）。ReloadChanged で、すぐ調べることもできる。
//   - コレクションは、名前で、いつでも取れる（無ければ作る）。ここで読み込んだマテリアルは、コレクションを、名前で自動で結ぶ。
//   - 能力（FMaterialCapabilities）は、これから読み込むマテリアルに使う。既定は DefaultCapabilities（DxLib 相当）。
//=============================================================================
class CMaterialSubsystem : public CAppSubsystem, public ITickable
{
public:
	CMaterialSubsystem();
	~CMaterialSubsystem() override;

	// 終了のとき、読み込んだマテリアルとコレクションを手放す（マテリアルが持つシェーダーを、アセットの後ろ盾が終わる前に解放するため）
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// ITickable
	//-------------------------------------------------------------------------
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override;

	// 時間（秒。起動からの、Tick の合計）と、前のフレームからの秒
	float32 GetTime() const { return Time; }
	float32 GetDeltaTime() const { return DeltaTime; }
	// いまの時間を入れた、毎フレームの状態（Hover などは 0。呼ぶ側が決める）
	FMaterialFrameState MakeFrameState() const;

	const FMaterialCapabilities& GetCapabilities() const { return Capabilities; }
	void SetCapabilities( const FMaterialCapabilities& InCapabilities ) { Capabilities = InCapabilities; }

	// ---- コレクション ----
	TSharedPtr<CMaterialParameterCollection> GetParameterCollection( const FString& Name );          // 無ければ作る
	TSharedPtr<CMaterialParameterCollection> FindParameterCollection( const FString& Name ) const;   // 無ければ null
	// .gxmaterialparametercollection を読んで、その名前（ファイルの Name。無ければ、パスから）のコレクションに入れる。読めなければ null
	TSharedPtr<CMaterialParameterCollection> LoadParameterCollection( const FString& Path );

	// ---- マテリアル ----
	// 定義から作る（キャッシュしない。コレクションの自動の結び付けと、能力は、ここのもの）
	TSharedPtr<CMaterial> CreateMaterial( const FMaterialDefinition& Definition );
	// ファイルから読む（キャッシュする）。.gxmaterial か .gxmaterialinstance。読めなければ null
	TSharedPtr<CMaterial> LoadMaterial( const FString& Path );
	// 読み込んだマテリアルの数
	int32 NumLoadedMaterials() const { return Loaded.Num(); }

	// ---- ホットリロード ----
	void SetHotReloadInterval( float32 Seconds ) { HotReloadInterval = Seconds; }
	// 読み込んだファイルを調べて、変わっていたものを作り直す。作り直した数を返す
	int32 ReloadChanged();

private:
	struct FLoaded
	{
		FString Path;
		FString Text;
		TSharedPtr<CMaterial> Material;
		bool bInstance = false;
	};

	bool ReadText( const FString& Path, FString& OutText ) const;
	void AttachResolver( CMaterial& Material );
	// ファイルの中身から、マテリアル（またはインスタンス）を作る / 作り直す。失敗したら false（OutError に理由）
	bool Build( FLoaded& Entry, const FString& Text, bool bReload, FString* OutError );

	float32 Time = 0.0f;
	float32 DeltaTime = 0.0f;
	float32 HotReloadInterval = 0.5f;
	float32 HotReloadTimer = 0.0f;
	FMaterialCapabilities Capabilities = NMaterial::DefaultCapabilities();
	TArray<TSharedPtr<CMaterialParameterCollection>> ParameterCollections;
	TArray<FLoaded> Loaded;
	TSharedPtr<bool> Alive = MakeShared<bool>( true );
};
