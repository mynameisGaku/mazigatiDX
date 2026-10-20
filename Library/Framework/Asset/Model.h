#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"
#include "Library/Math/Vector3.h"

class CModel;
class CModelInstance;

//=============================================================================
// FModelClipInfo
//   モデルのファイルに入っているアニメーション 1 つ。
//=============================================================================
struct FModelClipInfo
{
	FString Name;           // ファイルに付いている名前（無ければ空）
	int32 AnimIndex = 0;    // モデルの中での番号
	float32 Length = 0.0f;  // 長さ（DxLib の時間単位。FBX / MV1 / X ではフレーム数）
};

//=============================================================================
// CModel
//   3D モデル（MV1 / FBX / X / PMD / PMX / MQO など）。CAssetManager::LoadModel で読み込む。
//   アニメーションだけが入ったファイル（モーション専用の FBX など）も、同じようにモデルとして読み込む。
//
//   TSharedPtr<CModel> Robot = Assets->LoadModel( "Models/Robot.fbx" );
//   TSharedPtr<CModel> Walk = Assets->LoadModel( "Models/Robot_Walk.fbx" );   // 動きだけのファイル
//   for ( const FModelClipInfo& Clip : Walk->GetClips() ) ...
//
//   CModel 自身は、読み込んだ元のデータ（共有される）。画面に出すときは CreateInstance() で自分用の
//   インスタンス（位置やアニメーションの状態を持つ）を作る。インスタンスは FModelAnimator が使う。
//=============================================================================
class CModel : public CAsset, public TSharedFromThis<CModel>
{
public:
	CModel( const FString& InPath, int32 InHandle, bool bInAsync );
	~CModel() override;

	// 元のデータのハンドル（直接描かない。描くのは CreateInstance() で作ったインスタンス）
	FORCEINLINE int32 GetHandle() const { return Handle; }

	bool IsReady() const override;
	FString Describe() const override;
	// 頂点・テクスチャ・フレームの推定（読み込みが終わってから）
	int64 GetMemoryBytes() const override;

	// アニメーション一覧。読み込みが終わるまで空
	const TArray<FModelClipInfo>& GetClips() const;
	// 名前で探す。無ければ nullptr
	const FModelClipInfo* FindClip( const FString& Name ) const;

	// メッシュの数（読み込みが終わるまで 0）。メッシュが無く、アニメーションだけが入ったファイル（モーション専用）は、
	// 画面に出せない（CreateInstance は失敗する）。別のモデルのアニメーションの元として使う
	int32 GetMeshCount() const;
	FORCEINLINE bool HasMesh() const { return GetMeshCount() > 0; }
	// すべてのメッシュを囲む箱（モデルのローカル座標）。読み込みが終わっていて、メッシュがあって、バックエンドが答えられるときだけ true
	bool GetBounds( FVector3& OutMin, FVector3& OutMax ) const;
	// すべてのメッシュの三角形（モデルのローカル座標。描くときと同じ位置）。当たり判定を作るときや、エディタの枠線のために、必要なときだけ取る（大きいので覚えない）。
	// Indices は 3 つで 1 三角形。読み込みが終わっていて、メッシュがあって、バックエンドが答えられるときだけ true
	bool GetCollisionMesh( TArray<FVector3>& OutVertices, TArray<uint32>& OutIndices ) const;
	// 読み込みが終わっていて、メッシュが無く、アニメーションがある
	bool IsAnimationOnly() const;
	// 骨（フレーム）の名前。アニメーションは、名前が合う骨だけを動かす
	const TArray<FString>& GetBoneNames() const;
	// このモデルの骨に、AnimSource のアニメーションの骨がどれだけ合うか（モーション専用ファイルを別のモデルに使うときの確認）
	FBoneMatch CompareBones( const CModel& AnimSource ) const;

	// 自分用のインスタンスを作る（元のデータを共有する）。失敗したら nullptr
	TSharedPtr<CModelInstance> CreateInstance();

private:
	void RefreshClips() const;
	void RefreshShape() const;

	int32 Handle;
	bool bAsync;
	mutable int64 MemoryBytes = -1;
	mutable bool bClipsRead = false;
	mutable TArray<FModelClipInfo> Clips;
	mutable bool bShapeRead = false;
	mutable int32 MeshCount = 0;
	mutable TArray<FString> Bones;
	mutable bool bHasBounds = false;
	mutable FVector3 BoundsMin = FVector3::Zero;
	mutable FVector3 BoundsMax = FVector3::Zero;
};

//=============================================================================
// CModelInstance
//   画面に出す 1 体分のモデル。位置・アニメーションの状態はインスタンスごとに別。
//   どのアニメーションを付けているか（アタッチ）を覚えていて、同じ動きを重ねて付けない。
//
//   通常は FModelAnimator が管理するので、直接使うことは少ない。
//=============================================================================
class CModelInstance : public CObject
{
public:
	CModelInstance( const TSharedPtr<CModel>& InSource, int32 InHandle );
	~CModelInstance() override;

	FORCEINLINE int32 GetHandle() const { return Handle; }
	FORCEINLINE const TSharedPtr<CModel>& GetSource() const { return Source; }

	// AnimSource の AnimIndex 番のアニメーションを付ける（同じものが付いていればそのまま）。戻り値は付けた番号（失敗したら -1）
	int32 AttachAnim( const TSharedPtr<CModel>& AnimSource, int32 AnimIndex );
	// 付けたアニメーションを外す
	void DetachAnim( int32 AttachIndex );
	FORCEINLINE int32 GetNumAttached() const { return Attached.Num(); }

private:
	struct FAttached
	{
		TSharedPtr<CModel> AnimSource;   // 使っている間は、元のデータを手放さない
		int32 AnimIndex = 0;
		int32 AttachIndex = -1;
	};

	TSharedPtr<CModel> Source;
	int32 Handle;
	TArray<FAttached> Attached;
};
