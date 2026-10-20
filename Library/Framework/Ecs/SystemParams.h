#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Ecs/EcsWorld.h"
#include "Library/Framework/Ecs/TransformWorld.h"
#include "Library/Math/Transform.h"

//=============================================================================
// システムや ForEach の関数に渡せる引数
//   float32                  そのフレームの DeltaTime（値渡し）
//   FEntityRef               今処理しているエンティティ（値渡し。RequestDestroy / Has<T> / Get<T> / GetObject が使える）
//   CGameObject& / *         そのエンティティのゲームオブジェクト（ForEach などで直接触りたいとき）
//   FTransformAccess&        そのオブジェクトの CTransformComponent のローカル値（const なら読むだけ）
//   const FTransformView&    そのオブジェクトの CTransformComponent の値を読む窓口（ワールド値も読める。描画システム・ForEach 用）
//   T& / const T&            必須コンポーネント（持っているオブジェクトだけが対象になる）
//   T* / const T*            任意コンポーネント（持っていればポインタ、無ければ nullptr。対象からは外れない）
//   NEcs::TWithout<Ts...>    除外フィルタ（指定した型を 1 つでも持つオブジェクトを対象から外す）
//
//   描画システム（AddDrawSystem）だけが取れる引数:
//   FDrawContext&            描画の記録先（DxLib の代わりに、これに「何を描くか」を書く）
//   描画システムは、データを const T& / const T* で読むだけ（書き込めない）。DeltaTime・FEntityRef・FTransformAccess は取れない
//=============================================================================

//-----------------------------------------------------------------------------
// FEntityRef
//-----------------------------------------------------------------------------
class FEntityRef
{
public:
	FEntityRef() = default;
	FEntityRef( int32 InEntity, FEcsWorld* InWorld ) : Entity( InEntity ), World( InWorld ) {}

	// エンティティの番号（シーン内で、生きている間は一意）
	FORCEINLINE int32 GetEntity() const { return Entity; }
	FORCEINLINE CGameObject* GetObject() const { return World->GetEntityObject( Entity ); }

	template<typename T>
	FORCEINLINE const T* Get() const { return World->GetData<T>( Entity ); }

	template<typename T>
	FORCEINLINE bool Has() const { return World->HasData<T>( Entity ); }

	// このゲームオブジェクトの破棄を予約する。システムの実行が終わったあと、メインスレッドで CGameObject::Destroy() が呼ばれる
	// （システムの中から Destroy() を直接呼んではいけない）
	FORCEINLINE void RequestDestroy() const { World->RequestDestroy( Entity ); }

private:
	int32 Entity = INDEX_NONE;
	FEcsWorld* World = nullptr;
};

//-----------------------------------------------------------------------------
// FTransformAccess
//   システムの中から、CTransformComponent のローカル値を読み書きする窓口。
//   書き込みは、ワールド値を古くする処理を、システムの実行が終わったあとにまとめて行う（並列に動いてもぶつからない）。
//   ワールド値は読めない（ほかのオブジェクトが同時に書き換えているため）。必要なら、システムの外（Tick や ForEach）で読む。
//-----------------------------------------------------------------------------
class FTransformAccess
{
public:
	FTransformAccess() = default;
	FTransformAccess( FTransformNode* InNode, FTransformWorld* InWorld, int32 InSlot ) : Node( InNode ), World( InWorld ), Slot( InSlot ) {}

	FORCEINLINE const FTransform& GetLocal() const { return Node->Local; }
	FORCEINLINE const FVector3& GetLocalPosition() const { return Node->Local.Position; }
	FORCEINLINE const FQuaternion& GetLocalRotation() const { return Node->Local.Rotation; }
	FORCEINLINE const FVector3& GetLocalScale() const { return Node->Local.Scale; }

	FORCEINLINE void SetLocal( const FTransform& Local ) { Node->Local = Local; World->MarkDirtyDeferred( *Node, Slot ); }
	FORCEINLINE void SetLocalPosition( const FVector3& Position ) { Node->Local.Position = Position; World->MarkDirtyDeferred( *Node, Slot ); }
	FORCEINLINE void SetLocalRotation( const FQuaternion& Rotation ) { Node->Local.Rotation = Rotation; World->MarkDirtyDeferred( *Node, Slot ); }
	FORCEINLINE void SetLocalScale( const FVector3& Scale ) { Node->Local.Scale = Scale; World->MarkDirtyDeferred( *Node, Slot ); }

	FORCEINLINE void Translate( const FVector3& Delta ) { Node->Local.Position = Node->Local.Position + Delta; World->MarkDirtyDeferred( *Node, Slot ); }
	FORCEINLINE void Rotate( const FQuaternion& Delta ) { Node->Local.Rotation = ( Delta * Node->Local.Rotation ).Normalized(); World->MarkDirtyDeferred( *Node, Slot ); }

private:
	FTransformNode* Node = nullptr;
	FTransformWorld* World = nullptr;
	int32 Slot = INDEX_NONE;
};

//-----------------------------------------------------------------------------
// FTransformView
//   描画システムの中から、CTransformComponent の値を読む窓口（読むだけ）。ワールド値も読める。
//   描画システムが動く時点で、全オブジェクトのワールド値は最新に計算済み（FSystemScheduler が先に Update する）で、
//   描画システムは何も書き換えないので、複数のスレッドから同時に読んでもぶつからない。
//-----------------------------------------------------------------------------
class FTransformView
{
public:
	FTransformView() = default;
	explicit FTransformView( const FTransformNode* InNode ) : Node( InNode ) {}

	FORCEINLINE const FTransform& GetLocal() const { return Node->Local; }
	FORCEINLINE const FVector3& GetLocalPosition() const { return Node->Local.Position; }
	FORCEINLINE const FQuaternion& GetLocalRotation() const { return Node->Local.Rotation; }
	FORCEINLINE const FVector3& GetLocalScale() const { return Node->Local.Scale; }

	FORCEINLINE const FTransform& GetWorld() const
	{
		COLLECTION_CHECK( !Node->bDirty, "FTransformView: the world transform is stale (draw systems run after FTransformWorld::Update)" );
		return Node->World;
	}
	FORCEINLINE const FVector3& GetWorldPosition() const { return GetWorld().Position; }
	FORCEINLINE const FQuaternion& GetWorldRotation() const { return GetWorld().Rotation; }
	FORCEINLINE const FVector3& GetWorldScale() const { return GetWorld().Scale; }
	// ワールド行列（Scale * Rotate * Translate。DxLib の MATRIX に暗黙変換できる）
	FORCEINLINE FMatrix4x4 GetWorldMatrix() const { return GetWorld().ToMatrix(); }

private:
	const FTransformNode* Node = nullptr;
};
