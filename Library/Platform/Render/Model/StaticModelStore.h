// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Interfaces/Asset/TextureBackend.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Framework/Asset/Model/Import/StaticModelDefinition.h"
#include "Library/Framework/Asset/Model/ModelCollisionTypes.h"
#include "Library/Platform/Render/Model/ModelRig.h"

// 形状と画像を共有し、個体の状態（不透明度・アニメーション・骨の上書き・表示）と登録番号を所有する。GPUに依存しない。
//   骨組み（FModelRig）を持つので、スキンつきのモデルと、アニメーションを付けたモデルは、描くときに CPU で変形した頂点を貸す。
//   動かない個体（スキンも、アニメーションも、上書きも、非表示も無い）は、共有した形状をそのまま貸す。
//   描画の実行スレッドと、ゲームのスレッドの両方から呼ばれるので、公開の関数は、すべて排他する（借りた頂点は、次の呼び出しまで有効）。
class CStaticModelStore : public IStaticModelSource
{
public:
	// 画像の解放先の寿命を変えないよう、所有者を複写しない。
	CStaticModelStore() = default;
	CStaticModelStore( const CStaticModelStore& ) = delete;
	CStaticModelStore& operator=( const CStaticModelStore& ) = delete;
	// 左手系のSceneを登録する。Texturesは全個体の解放まで生存させる。
	// bAllowRigがtrueなら、スキン・アニメーション・メッシュの無い（アニメーションだけの）シーンも受け付ける。
	// 失敗時は-1と理由を返し、読み込み途中の画像を残さない。
	int32 Create( const FImportedScene& Scene, const FString& SourcePath, ITextureBackend& Textures, FString& Error, bool bAllowRig = false );
	// 形状と画像を共有し、不透明度と骨の上書きを値でコピーした個体を作る（付けたアニメーションは引き継がない）。
	int32 Duplicate( int32 Source );
	// 最後の共有個体を削除したとき画像も解放する。
	void Delete( int32 Handle );
	// 全個体を解放する。登録番号は再利用しない。
	void Reset();
	// 登録中の個体数。
	int32 Num() const;
	// 同期の命令解釈中だけ形状を貸す（動く個体は、いまのポーズで変形した頂点）。
	bool GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out ) override;
	// 描くときの配置（3 x 4。FDrawModelData::M と同じ並び）を覚える（フレームのワールド行列・位置に使う）。
	bool SetStaticModelPlacement( int32 Handle, const float32 ( &Matrix )[12] ) override;
	// 付けたアニメーションの時間（30 = 1 秒）・割合・骨ごとの割合。付けていない番号は false。
	bool SetStaticModelAnimTime( int32 Handle, int32 AttachIndex, float32 Time ) override;
	bool SetStaticModelAnimRate( int32 Handle, int32 AttachIndex, float32 Rate ) override;
	bool SetStaticModelAnimFrameRate( int32 Handle, int32 AttachIndex, int32 Frame, float32 Rate, bool bIncludeChildren ) override;
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

	//-------------------------------------------------------------------------
	// アニメーション・骨（IModelBackend の対応する窓口が使う）
	//-------------------------------------------------------------------------
	int32 GetAnimCount( int32 Handle ) const;
	FString GetAnimName( int32 Handle, int32 AnimIndex ) const;
	// 長さ（30 = 1 秒の単位）。無ければ 0
	float32 GetAnimLength( int32 Handle, int32 AnimIndex ) const;
	// Source のクリップ AnimIndex を、Handle に付ける（骨は名前で結びつける）。付けられなければ -1
	int32 AttachAnim( int32 Handle, int32 AnimIndex, int32 Source );
	void DetachAnim( int32 Handle, int32 AttachIndex );
	// アニメーションを付けずに、クリップ Source / AnimIndex の、骨 Frame（Handle のノードの番号）の Time（30 = 1 秒）の行列を、名前で結びつけて求める
	bool SampleAnimFrameLocal( int32 Handle, int32 AttachIndex, int32 Frame, float32 Time, FMatrix4x4& Out ) const;
	// 骨の行列（いまのポーズ）。ワールドは、描いたときの配置を掛けた値
	bool GetFrameMatrix( int32 Handle, int32 Frame, bool bWorld, FMatrix4x4& Out );
	bool GetFramePosition( int32 Handle, int32 Frame, float32 ( &Out )[3] );
	// 骨の「親から見た行列」の上書き
	bool GetFrameOverride( int32 Handle, int32 Frame, bool& bOutOverridden, FMatrix4x4& Out ) const;
	bool SetFrameOverride( int32 Handle, int32 Frame, const FMatrix4x4& Local );
	bool ClearFrameOverride( int32 Handle, int32 Frame );
	// 骨（子も含めて）・メッシュの表示
	bool GetFrameVisible( int32 Handle, int32 Frame, bool& bOut ) const;
	bool SetFrameVisible( int32 Handle, int32 Frame, bool bVisible );
	bool GetMeshVisible( int32 Handle, int32 Mesh, bool& bOut ) const;
	bool SetMeshVisible( int32 Handle, int32 Mesh, bool bVisible );
	// 元のメッシュ Mesh（フレーム Frame の中）が、いま描かれるか（メッシュ自身と、親までのフレームの表示を合わせる）。
	bool GetMeshDrawn( int32 Handle, int32 Mesh, int32 Frame, bool& bOut ) const;
	// メッシュの中身（頂点と三角形）。Mesh は元のメッシュの番号で、-1 なら全部。bCurrentPose が true なら、いまの姿勢（アニメーション・骨の上書きを含む）の位置、false なら読み込んだままの位置。
	// 番号が範囲の外・メッシュが無いときは false（Out は変えない）。頂点は、使う頂点だけを、元の並びで入れる。
	bool GetMeshData( int32 Handle, int32 Mesh, bool bCurrentPose, FModelMeshData& Out );
	// 骨組みのあるモデルか（形状の無い、アニメーションだけのモデルも）
	bool HasRig( int32 Handle ) const;

	//-------------------------------------------------------------------------
	// 当たり判定（モデルの手元の座標。置き場所を含まない）
	//   準備したときの形（いまのポーズで変形した頂点。非表示のメッシュも含む）を写しておき、それに問い合わせる。
	//   準備していなければ、最初の問い合わせのときに準備する。準備し直すまで、形は変わらない。
	//-------------------------------------------------------------------------
	bool PrepareCollision( int32 Handle );
	// From から To への線分が、最初に当たる三角形。当たらなければ false
	bool Raycast( int32 Handle, const FVector3& From, const FVector3& To, FModelHit& Out );
	// 球と重なる三角形をすべて（Distance は、球の中心から三角形のいちばん近い点まで）
	bool OverlapSphere( int32 Handle, const FVector3& Center, float32 Radius, TArray<FModelHit>& Out );

	//-------------------------------------------------------------------------
	// 材質の色（個体ごとに上書きできる。描画に反映するのは拡散色（Diffuse）だけ）
	//-------------------------------------------------------------------------
	bool GetMaterialColors( int32 Handle, int32 Material, FModelMaterialColors& Out ) const;
	bool SetMaterialColors( int32 Handle, int32 Material, const FModelMaterialColors& Colors );
private:
	// 形状は登録時に完成し、以降は書き換えない。
	struct FResource
	{
		// 途中の読み込み失敗でも、取得済みの画像を戻す。
		~FResource();
		FStaticModelDefinition Definition; // 面データとフレーム情報。
		TArray<FDrawVertex3D> Vertices; // 共通描画向けの頂点（基準の姿。スキンつきは、基準の姿に変形した値）。
		TArray<FStaticModelDrawSection> Sections; // 共通描画向けの材質範囲。
		TArray<int32> OwnedTextures; // 最後の個体まで所有する画像。
		ITextureBackend* TextureBackend = nullptr; // 所有画像の解放先。
		int64 Bytes = 0; // 形状のメモリ推定。
		TSharedPtr<const FModelRig> Rig; // 骨組み（骨・スキン・クリップ）。
		TArray<FModelVertexSource> VertexSources; // Vertices と同じ並びの、元のメッシュと頂点。
		bool bHasSkin = false; // スキンつきのメッシュがある（必ず、変形した頂点で描く）。
		uint64 Id = 0; // 形状の識別（GPU の共有メッシュの鍵。解放後も再利用しない）。
	};
	// 個体ごとのポーズと、変形した頂点。
	struct FInstanceRig
	{
		FModelPose Pose;
		TArray<FDrawVertex3D> Vertices; // 変形した頂点。
		TArray<uint32> Indices; // 非表示を除いた index。
		TArray<FStaticModelDrawSection> Sections; // 非表示を除いた範囲。
		TArray<FMatrix4x4> Local;
		TArray<FMatrix4x4> Global;
		uint64 PoseVersion = 0; // Local / Global を作ったときの Pose.Version。
		uint64 BuiltVersion = 0; // Vertices / Indices / Sections を作ったときの Pose.Version。
		TArray<FModelMaterialColors> MaterialColors; // 個体ごとの材質の色（上書きしたものだけ有効）。
		TArray<uint8> bMaterialSet;
	};
	// 準備したときの形の三角形。
	struct FCollisionTriangle
	{
		FVector3 P[3];
		FVector3 Normal; // 頂点の法線の向きに揃えた、三角形の向き。
		FVector3 Min, Max;
		int32 Frame = -1;
		int32 Material = -1;
		int32 Triangle = -1; // フレームの中での三角形の番号。
	};
	struct FCollision
	{
		TArray<FCollisionTriangle> Triangles;
	};
	// 形状の共有と個体別の値を分ける。
	struct FInstance
	{
		TSharedPtr<FResource> Resource; // 個体間で共有する不変形状。
		float32 Opacity = 1; // この個体だけの不透明度。
		TSharedPtr<FInstanceRig> Rig; // この個体だけのポーズ（共有しない）。
		TSharedPtr<FCollision> Collision; // 準備した当たり判定（共有しない）。
	};
	// 以下は、排他を取った中でだけ呼ぶ。
	FInstance* FindInstance( int32 Handle );
	const FInstance* FindInstance( int32 Handle ) const;
	void EnsurePose( FInstance& Instance );
	bool IsHidden( const FInstance& Instance, int32 SourceMesh, int32 SourceFrame ) const;
	bool NeedsDeform( const FInstance& Instance ) const;
	void BuildDeformed( FInstance& Instance );
	// 非表示を除かない、いまの姿の頂点（当たり判定の準備に使う）。
	void GetFullVertices( FInstance& Instance, TArray<FDrawVertex3D>& Out );
	bool EnsureCollision( FInstance& Instance );

	mutable FMutex Lock; // 描画の実行スレッドとゲームのスレッドの両方から呼ばれる。
	TMap<int32, FInstance> Instances; // 登録番号から個体への表。
	int32 NextHandle = 0; // 解放後も再利用しない登録番号。
	uint64 NextResourceId = 1; // 解放後も再利用しない形状の識別。
};
