#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Component/Component.h"
#include "Library/Framework/Ecs/TransformWorld.h"
#include "Library/Math/Transform.h"

//=============================================================================
// CTransformComponent
//   位置・回転・スケール。親オブジェクトも CTransformComponent を持っていれば、
//   ローカル値は親からの相対になる（持っていない親は飛ばしてさらに上を探す）。
//
//   CTransformComponent* Transform = Object->AddComponent<CTransformComponent>();
//   Transform->SetLocalPosition( { 2, 0, 0 } );
//   FVector3 World = Transform->GetWorldPosition();
//
//   ワールド値は「読んだ時点で常に最新」（親を動かしても古くならない）。
//   スケールは成分ごとの掛け算なので、回転した親の非一様スケールによるせん断は表現しない。
//
//   中身（ECS）: 値の実体はシーンの FTransformWorld にあり、このクラスはそれを指す窓口。
//   ワールド値は階層の浅い順にまとめて計算して持っておく（シーンが描画の前に、ジョブシステムで複数のスレッドを使って計算する）ので、
//   描画などで大量に読んでも軽い。使い方は以前と変わらない。
//   シーンに入る前（コンストラクタで AddComponent したときなど）は、このコンポーネントが値を持ち、シーンに入ると移る。
//=============================================================================
class CTransformComponent : public CComponent
{
public:
	//-------------------------------------------------------------------------
	// ローカル（親からの相対）
	//-------------------------------------------------------------------------
	FORCEINLINE const FTransform& GetLocalTransform() const { return Node != nullptr ? Node->Local : DetachedLocal; }
	void SetLocalTransform( const FTransform& InLocal );

	FORCEINLINE const FVector3& GetLocalPosition() const { return GetLocalTransform().Position; }
	void SetLocalPosition( const FVector3& Position );

	FORCEINLINE const FQuaternion& GetLocalRotation() const { return GetLocalTransform().Rotation; }
	void SetLocalRotation( const FQuaternion& Rotation );

	FORCEINLINE const FVector3& GetLocalScale() const { return GetLocalTransform().Scale; }
	void SetLocalScale( const FVector3& Scale );

	//-------------------------------------------------------------------------
	// ワールド
	//-------------------------------------------------------------------------
	FTransform GetWorldTransform() const;
	FORCEINLINE FVector3 GetWorldPosition() const { return Node != nullptr ? World->GetWorld( Slot ).Position : DetachedLocal.Position; }
	// 親のワールド位置を考慮してローカル位置を設定する
	void SetWorldPosition( const FVector3& WorldPosition );

	// 祖先で最も近い CTransformComponent（無ければ nullptr）
	const CTransformComponent* FindParentTransform() const;

	//-------------------------------------------------------------------------
	// CComponent / CObject
	//-------------------------------------------------------------------------
	void OnAttach() override;
	void OnDetach() override;
	void OnOwnerSceneChanged( CScene* NewScene ) override;
	void OnDestroyRequested() override;
	void InspectProperties( IPropertyInspector& Inspector ) override;

private:
	// シーンのデータへ移る / 戻る
	void BindToWorld( FTransformWorld& InWorld );
	void UnbindFromWorld();

private:
	FTransformWorld* World = nullptr;   // 値を持っているシーンのデータ（シーンに入る前は nullptr）
	int32 Slot = INDEX_NONE;
	FTransformNode* Node = nullptr;     // World の中の自分の要素（プールの要素はアドレスが動かないので、直接指しておく）
	FTransform DetachedLocal = FTransform::Identity;   // World が nullptr の間の値
};
