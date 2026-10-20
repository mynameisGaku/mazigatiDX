#pragma once
#include <functional>
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SmartPointer.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Math/Transform.h"

class CAssetManager;

//=============================================================================
// ワールドをシーンに置く（設置）
//   定義（FWorldDefinition）を読んで、シーンにオブジェクトを作る。1 つの定義のオブジェクト 1 つにつき、シーンに「置き物」（CGameObject）が 1 つできる。
//   置き物は、定義の位置・向き・大きさ（親があれば、親の子）と、定義の部品（モデル・ビルボード・環境音・当たり判定）を持つ。
//
//   // 1. 読み込んで、置く
//   TSharedPtr<CWorldAsset> Level = Assets->LoadWorld( "Levels/Stage1.world" );
//   FWorldBindings Bindings;
//   Bindings.Add<CEnemy>( "Enemy" );                                       // タグ "Enemy" の置き物に、CEnemy を結び付ける（置き物の子になる）
//   Bindings.Add( "Coin", []( const FWorldBindContext& Context )           // 作り方を自分で決めるなら
//   {
//       return Context.Scene.SpawnObject<CCoin>( Context.Definition.FindProp( "Value" )->IntValue );
//   } );
//   FWorldInstance Stage = NWorld::Spawn( Scene, Level->GetDefinitionShared(), Bindings );
//
//   // 2. コードから探す
//   TSharedPtr<CGameObject> Start = Stage.FindFirst( "PlayerStart" );     // タグから（置き物）
//   for ( const TSharedPtr<CEnemy>& Enemy : Stage.FindAllBound<CEnemy>( "Enemy" ) ) ...   // 結び付けたもの
//   Stage.Despawn();                                                       // まとめて消す
//
//   空のオブジェクト（部品なし）にタグを付けておくと、「ここに、これを置く」という目印になる（スポーン地点・敵の出現位置・トリガーの位置）。
//
//   部品の設置は、レジストリ（FWorldSpawnerRegistry）に登録された部品ごとの設置関数が行う。Framework が持つのは Model / Billboard / Sound だけで、
//   物理（Collider）は Physics 側が登録する（NPhysicsWorld::RegisterSpawners。Framework は Physics を知らない）。
//   登録の無い種類の部品は、置かれず、警告になる。
//
//   設置のあとの置き物は、ふつうのオブジェクト（Tick / Draw / Destroy など）。FWorldInstance は弱い参照だけを持つ。
//=============================================================================

// 設置の途中で出た問題と、数
struct FWorldSpawnReport
{
	TArray<FString> Warnings;
	int32 NumSpawned = 0;     // 置いた置き物の数
	int32 NumDisabled = 0;    // bEnabled が false で、置かなかったオブジェクトの数（子孫を含む）
	int32 NumBound = 0;       // タグに結び付けて作ったオブジェクトの数
	int32 NumSkipped = 0;     // 親の循環などで、置けなかったオブジェクトの数
	int32 NumPrefabs = 0;     // 置いた入れ子のワールド（Prefab 部品）の数。入れ子の中のものを含む（NumSpawned・NumBound・NumDisabled・NumSkipped・警告も、入れ子の中のものを含む）
};

// タグに結び付けた作り方に渡す情報
struct FWorldBindContext
{
	CScene& Scene;
	const FWorldDefinition& World;
	const FWorldObject& Definition;     // 置く場所のオブジェクトの定義（プロパティなどを読める）
	CGameObject& Placeholder;           // 置き物（作ったオブジェクトは、この子になる）
	FString Tag;                        // 一致したタグ
};

// 作ったオブジェクトを返す（置き物の子として、置き物と同じ位置・向きに付く）。失敗したら空
using FWorldBindFactory = std::function<TSharedPtr<CGameObject>( const FWorldBindContext& )>;

//-----------------------------------------------------------------------------
// FWorldBindings
//   タグ -> コード側の生成物。1 つのタグに 1 つの作り方（同じタグに足すと置き換わる）。
//   1 つの置き物に複数のタグがあれば、一致したタグの数だけ作る（タグの付いた順）。
//-----------------------------------------------------------------------------
class FWorldBindings
{
public:
	void Add( const FString& Tag, FWorldBindFactory Factory );

	// T を、引数なしの SpawnObject<T>() で作る
	template<typename T>
	void Add( const FString& Tag )
	{
		static_assert( std::is_base_of_v<CGameObject, T>, "FWorldBindings::Add<T>: T must derive from CGameObject" );
		Add( Tag, []( const FWorldBindContext& Context ) { return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<T>() ); } );
	}

	const FWorldBindFactory* Find( const FString& Tag ) const;
	bool Has( const FString& Tag ) const { return Find( Tag ) != nullptr; }
	int32 Num() const { return Entries.Num(); }

private:
	struct FEntry
	{
		FString Tag;
		FWorldBindFactory Factory;
	};
	TArray<FEntry> Entries;
};

//-----------------------------------------------------------------------------
// 部品ごとの設置
//-----------------------------------------------------------------------------
struct FWorldSpawnContext
{
	CScene& Scene;
	const FWorldDefinition& World;
	const FWorldObject& Definition;
	CGameObject& Object;                // 置き物
	FTransform WorldTransform;          // 置き物の、ワールドでの位置・向き・大きさ（定義から計算したもの。設置の時点で確定している）
	CAssetManager* Assets = nullptr;    // アセットの読み込み用。動いていなければ nullptr
	FWorldSpawnReport& Report;

	// 警告を、報告とログに出す（オブジェクトの名前と Id がつく）
	void Warn( const FString& Message ) const;
};

using FWorldComponentSpawner = std::function<void( FWorldSpawnContext& )>;

//-----------------------------------------------------------------------------
// FWorldSpawnerRegistry
//   部品の種類の名前（"Model" "Billboard" "Sound" "Collider" ...）-> 設置する関数。
//   関数は、オブジェクトごとに、登録した順に呼ばれる（その部品を持たないオブジェクトでは、何もせずに戻る）。
//   Global() には、Framework の組み込み（Model / Billboard / Mesh / Sound / Effect）が入っている。物理などは、使う側が Add する。
//-----------------------------------------------------------------------------
class FWorldSpawnerRegistry
{
public:
	// 同じ名前があれば置き換える（順番は、最初の登録のまま）
	void Add( const FString& Name, FWorldComponentSpawner Spawner );
	bool Remove( const FString& Name );
	bool Has( const FString& Name ) const;
	int32 Num() const { return Entries.Num(); }

	struct FEntry
	{
		FString Name;
		FWorldComponentSpawner Spawner;
	};
	const TArray<FEntry>& GetEntries() const { return Entries; }

	// アプリ全体で使う、組み込みが入ったレジストリ
	static FWorldSpawnerRegistry& Global();
	// 組み込み（Model / Billboard / Mesh / Sound / Effect）だけが入った、新しいレジストリ
	static FWorldSpawnerRegistry CreateWithBuiltins();

private:
	TArray<FEntry> Entries;
};

namespace NWorld
{
	// Framework の組み込みの部品（Model / Billboard / Mesh / Sound / Effect）の設置を登録する
	void RegisterBuiltinSpawners( FWorldSpawnerRegistry& Registry );
}

//-----------------------------------------------------------------------------
// 置き物に付ける ECS データ（どの定義の、どのオブジェクトか）
//   ゲームのコードが、置き物（や、衝突の相手）から、タグ・プロパティを読むのに使う。
//
//   if ( const FWorldObjectRef* Ref = Object.GetData<FWorldObjectRef>() ) { const FWorldObject* Definition = Ref->Get(); ... }
//-----------------------------------------------------------------------------
struct FWorldObjectRef
{
	TSharedPtr<FWorldDefinition> World;
	int32 ObjectId = WorldNoId;

	const FWorldObject* Get() const { return World.IsValid() ? World->FindObject( ObjectId ) : nullptr; }
};

//-----------------------------------------------------------------------------
// FWorldInstance
//   置いた結果。置き物とタグで結び付けたオブジェクトを、コードから探す窓口。弱い参照だけを持つので、
//   消えたオブジェクトは、見つからなくなる（Despawn しなくてもよい）。
//-----------------------------------------------------------------------------
class FWorldInstance
{
public:
	// 置き物（定義のオブジェクト 1 つに 1 つ）。無い・消えていれば空
	TSharedPtr<CGameObject> FindById( int32 ObjectId ) const;
	// 最初に見つかった、そのタグの置き物（定義の並びの順）。大文字小文字を区別する
	TSharedPtr<CGameObject> FindFirst( const FString& Tag ) const;
	TArray<TSharedPtr<CGameObject>> FindAll( const FString& Tag ) const;
	TSharedPtr<CGameObject> FindByName( const FString& Name ) const;

	// 入れ子のワールド（Prefab 部品）として置いた結果。HostId は、Prefab 部品を持つオブジェクトの Id。無ければ nullptr
	//   FindAll / FindFirst / FindByName は、入れ子の中も探す（自分のものが先、次に入れ子の中。FindById と FindAllBound は、自分のものだけ）。
	const FWorldInstance* FindNested( int32 HostId ) const;
	int32 GetNumNested() const { return Nested.Num(); }

	// そのタグに結び付けて作ったオブジェクト（T にキャストできるもの）
	template<typename T>
	TSharedPtr<T> FindFirstBound( const FString& Tag ) const
	{
		TArray<TSharedPtr<T>> All = FindAllBound<T>( Tag );
		return All.IsEmpty() ? TSharedPtr<T>() : All[0];
	}

	template<typename T>
	TArray<TSharedPtr<T>> FindAllBound( const FString& Tag ) const
	{
		TArray<TSharedPtr<T>> Result;
		for ( const FEntry& Entry : Entries )
		{
			for ( const FBound& Bound : Entry.Bound )
			{
				if ( Bound.Tag != Tag ) continue;
				if ( TSharedPtr<CGameObject> Object = Bound.Object.Pin(); Object.IsValid() && Object->IsAlive() && Object->template IsA<T>() ) Result.Add( StaticCastSharedPtr<T>( Object ) );
			}
		}
		return Result;
	}

	// 置いた定義（置き物に付く FWorldObjectRef と同じもの）
	const TSharedPtr<FWorldDefinition>& GetDefinition() const { return Definition; }
	// 置いた置き物の数（定義のオブジェクトのうち、実際に置いたもの）
	int32 GetNumSpawned() const { return Entries.Num(); }
	const FWorldSpawnReport& GetReport() const { return Report; }

	// 置いた置き物を、すべて消す（子も。破棄の予約）。空になる
	void Despawn();

	//-------------------------------------------------------------------------
	// 内部用（NWorld::Spawn が、置きながら記録するために使う）
	//-------------------------------------------------------------------------
	void SetDefinition( const TSharedPtr<FWorldDefinition>& InDefinition ) { Definition = InDefinition; }
	void AddPlaceholder( int32 ObjectId, const TSharedPtr<CGameObject>& Placeholder );
	void AddBound( int32 ObjectId, const FString& Tag, const TSharedPtr<CGameObject>& Object );
	void AddNested( int32 HostId, const TSharedPtr<FWorldInstance>& Instance );
	FWorldSpawnReport& EditReport() { return Report; }

private:
	struct FBound
	{
		FString Tag;
		TWeakPtr<CGameObject> Object;
	};
	struct FEntry
	{
		int32 ObjectId = WorldNoId;
		TWeakPtr<CGameObject> Placeholder;
		TArray<FBound> Bound;
	};

	struct FNestedEntry
	{
		int32 HostId = WorldNoId;
		TSharedPtr<FWorldInstance> Instance;
	};

	TSharedPtr<FWorldDefinition> Definition;
	TArray<FEntry> Entries;
	TArray<FNestedEntry> Nested;
	FWorldSpawnReport Report;
};

struct FWorldSpawnOptions
{
	// 使う部品のレジストリ。nullptr なら FWorldSpawnerRegistry::Global()
	const FWorldSpawnerRegistry* Registry = nullptr;
	// アセットの読み込み先。nullptr なら、動いている CAssetManager（無ければ、アセットを使う部品は置かれない）
	CAssetManager* Assets = nullptr;
	// ワールド全体の置き場所（最上位のオブジェクトの位置に掛かる。同じレベルを、別の場所に置けるように）
	FTransform Root = FTransform::Identity;
	// true なら、bEnabled が false のオブジェクトも置く
	bool bSpawnDisabled = false;
	// いちばん外側の定義のファイル（アセットのルートからの相対）。入れ子（Prefab 部品）の循環の検出に使う（自分自身を含む循環を、置かずに済ませる）。分からなければ空
	FString WorldPath;
	// 内部用: 入れ子を置くときの、ここまでの道筋（入れ子のファイルの、正規化したパス。WorldPath を含まない。WorldMaxPrefabDepth 個まで）と、最上位のオブジェクトの付け先（Root は、付け先のワールドでの位置になる）
	TArray<FString> PrefabStack;
	CGameObject* AttachTo = nullptr;
};

namespace NWorld
{
	// 定義をシーンに置く。Definition は、置き物が参照し続ける（FWorldObjectRef）ので、共有のポインタで渡す
	FWorldInstance Spawn( CScene& Scene, const TSharedPtr<FWorldDefinition>& Definition, const FWorldBindings& Bindings = FWorldBindings(), const FWorldSpawnOptions& Options = FWorldSpawnOptions() );
	// 定義のコピーを作って置く
	FWorldInstance Spawn( CScene& Scene, const FWorldDefinition& Definition, const FWorldBindings& Bindings = FWorldBindings(), const FWorldSpawnOptions& Options = FWorldSpawnOptions() );
}
