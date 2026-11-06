#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/World/Definition/WorldDefinition.h"
#include "Library/Framework/World/Definition/WorldValidate.h"

class CAssetSubsystem;

//=============================================================================
// CWorldAsset
//   読み込んだワールド（.gxworld）。CAssetSubsystem::LoadWorld で読み込む。中身は FWorldDefinition（純粋なデータ。WorldDefinition.h）。
//   シーンへの設置は NWorld::Spawn（WorldSpawner.h）。
//
//   TSharedPtr<CWorldAsset> Level = Assets->LoadWorld( "Levels/Stage1.gxworld" );
//   FWorldInstance Stage = NWorld::Spawn( Scene, Level->GetDefinitionShared(), Bindings );
//
//   定義を作り直して保存するときは Assets->SaveWorld( Path, Definition )（保存したパスのキャッシュは捨てるので、次の LoadWorld は新しい内容を読む）。
//=============================================================================
class CWorldAsset : public CAsset
{
public:
	CWorldAsset( const FString& InPath, const FWorldDefinition& InDefinition, const TArray<FString>& InLoadWarnings );

	FORCEINLINE const FWorldDefinition& GetDefinition() const { return *Definition; }
	// 置き物が参照し続けるので、設置には、こちらを渡す
	FORCEINLINE const TSharedPtr<FWorldDefinition>& GetDefinitionShared() const { return Definition; }
	// 読み込みのとき、直したこと・読み飛ばしたこと
	FORCEINLINE const TArray<FString>& GetLoadWarnings() const { return LoadWarnings; }

	FString Describe() const override;
	int64 GetMemoryBytes() const override;

private:
	TSharedPtr<FWorldDefinition> Definition;
	TArray<FString> LoadWarnings;
};

//=============================================================================
// FAssetWorldQuery
//   IWorldAssetQuery（検証が、アセットの有無を調べる窓口）の、CAssetSubsystem による実装。
//   .fbx / .obj は、元のファイルが無くても、変換済みの .mv1 があればよい（配布版）。
//=============================================================================
class FAssetWorldQuery : public IWorldAssetQuery
{
public:
	explicit FAssetWorldQuery( const CAssetSubsystem& InAssets ) : Assets( InAssets ) {}
	bool Exists( const FString& Path ) const override;

private:
	const CAssetSubsystem& Assets;
};
