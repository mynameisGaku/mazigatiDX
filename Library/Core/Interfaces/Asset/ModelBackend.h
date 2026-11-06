// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"

//=============================================================================
// IModelBackend
//   3D モデル（読み込み・複製・アニメーション・構造・見た目・当たり判定・保存）を扱う窓口。モデルを使う側（CModelLoader / CModelImporter / CModelInstance）が知る
//   すべての関数はメインスレッドだけから呼ばれる（バックエンドがスレッドセーフとは限らない）。実装は IAssetBackend を通して持つ
//=============================================================================
class IModelBackend
{
public:
	virtual ~IModelBackend() = default;

	//-------------------------------------------------------------------------
	// 3D モデル（アニメーションだけが入ったファイルも、モデルとして読み込む）
	//-------------------------------------------------------------------------
	virtual int32 LoadModel( const FString& Path, bool bAsync ) = 0;
	// 同じデータを共有する、別の状態（位置・アニメーション）を持つモデルを作る
	virtual int32 DuplicateModel( int32 Source ) = 0;
	virtual void DeleteModel( int32 Model ) = 0;

	// モデルが持つアニメーション。名前が無ければ空
	virtual int32 GetModelAnimCount( int32 Model ) = 0;
	virtual FString GetModelAnimName( int32 Model, int32 AnimIndex ) = 0;
	// アニメーションの長さ（実装の時間単位。DxLib ではフレーム数）。不明なら 0
	virtual float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) = 0;
	// アニメーションの時間の単位（1 秒あたりの値。秒で扱う実装なら 1）。GetModelAnimLength とモデルの再生時間は、この単位で表す。
	// 分からなければ 0（呼ぶ側は、既定の 30 を使う）
	virtual float32 GetModelAnimUnitsPerSecond( int32 /*Model*/, int32 /*AnimIndex*/ ) { return 0.0f; }
	// Model に、SourceModel（Model 自身でもよい）の AnimIndex 番のアニメーションを付ける。戻り値は付けた番号（失敗したら -1）
	virtual int32 AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel ) = 0;
	virtual void DetachModelAnim( int32 Model, int32 AttachIndex ) = 0;
	// 専用modelの単一attached clipを指定時刻・weight1にして、指定frameの完全な親基準poseを読む。失敗時は false で Out を変えない。
	virtual bool SampleModelAnimFrameLocalMatrix( int32 /*Model*/, int32 /*AttachIndex*/, int32 /*Frame*/, float32 /*Time*/, FMatrix4x4& /*Out*/ ) { return false; }

	// 形の情報。メッシュが 0 個ならアニメーションだけのファイル（モーション専用）。読み込み中なら 0
	virtual int32 GetModelMeshCount( int32 Model ) = 0;
	// 静的モデルの面データを写す。非対応・失敗時は false とし、Outを変更しない。
	// 成功時はframe変換込み・world変換なしの位置とreference法線/UV0/頂点色を写し、三角形順を保つ。
	// sectionはindex列を隙間なく覆い、同一frame/mesh/materialの連続runだけをまとめる。
	// 照明・alpha test・UV変換等native描画の同等性は保証せず、借用画像handleを削除してはならない。
	// 借用handleは元Modelと同じbackendの寿命内だけ有効で、この関数はメインスレッドから呼ぶ。
	virtual bool GetStaticModelSurface( int32 /*Model*/, FStaticModelSurfaceData& /*Out*/ ) { return false; }
	// 指定材質の静的model面をbackend内の不変handleへ登録し、未対応・不正入力では-1を返す。
	// CPU値だけをコピーし、sourceや借用画像の所有権は呼出側が維持する。
	virtual int32 CreateModelMaterialGeometry( const FStaticModelSurfaceData& /*Surface*/, int32 /*MaterialIndex*/ ) { return -1; }
	// CreateModelMaterialGeometryで作った不変形状handleを解放する。呼出側はReleaseModelMaterialGeometryを使う。
	virtual void DeleteModelMaterialGeometry( int32 /*Geometry*/ ) {}
	// モデル全体のローカルでの大きさ（すべてのメッシュを囲む、軸に平行な箱）。メッシュが 0 個・読み込み中・分からないなら false
	virtual bool GetModelBounds( int32 /*Model*/, float32 ( & /*OutMin*/ )[3], float32 ( & /*OutMax*/ )[3] ) { return false; }
	// モデルの三角形（当たり判定用。モデルのローカル座標で、フレームの変換を適用した、描くときと同じ位置）。OutPositions は x, y, z の並び、OutIndices は 3 つで 1 三角形（DxLib の描画と同じ向き）。
	// メッシュが無い・読み込み中・分からないなら false
	virtual bool GetModelCollisionMesh( int32 /*Model*/, TArray<float32>& /*OutPositions*/, TArray<uint32>& /*OutIndices*/ ) { return false; }
	// 骨（フレーム）。アニメーションは名前が合う骨だけを動かすので、モデルとアニメーションの骨の名前が合うかの確認に使う
	virtual int32 GetModelFrameCount( int32 Model ) = 0;
	virtual FString GetModelFrameName( int32 Model, int32 FrameIndex ) = 0;
	// 骨（フレーム）の、いまの位置（ワールド座標。最後に描いたときのアニメーションと行列で決まる）。番号が範囲の外・分からないなら false
	virtual bool GetModelFramePosition( int32 /*Model*/, int32 /*FrameIndex*/, float32 ( & /*OutPosition*/ )[3] ) { return false; }
	// MV1 ファイルに保存する（Path は絶対パス。フォルダは作ってある前提）。成功したら true。
	// 使われていないアニメーションがあった、という警告つきの成功も true（OutWarning に理由）
	virtual bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) = 0;

	//-------------------------------------------------------------------------
	// モデルの構造と、モデル 1 体ごとの見た目（DuplicateModel で作ったものは、それぞれ別に持つ）
	//   対応していない実装・読み込み中・番号が範囲の外は false で、Out は変えない
	//-------------------------------------------------------------------------
	// フレーム・メッシュ・材質・テクスチャを写す
	virtual bool GetModelStructure( int32 /*Model*/, FModelStructure& /*Out*/ ) { return false; }
	// メッシュの中身（頂点と三角形）。Mesh は FModelStructure::Meshes の番号で、-1 なら全部。
	// bCurrentPose が true なら、いまの姿勢（アニメーション・骨の上書きを含む）の位置、false なら読み込んだままの位置。
	// 形状（モーフ）の変形は、DxLib の参照用メッシュに出ないので、どちらにも含まれない（描画には効く）
	virtual bool GetModelMeshData( int32 /*Model*/, int32 /*Mesh*/, bool /*bCurrentPose*/, FModelMeshData& /*Out*/ ) { return false; }
	// 材質の色（いまの値）
	virtual bool GetModelMaterialColors( int32 /*Model*/, int32 /*Material*/, FModelMaterialColors& /*Out*/ ) { return false; }
	virtual bool SetModelMaterialColors( int32 /*Model*/, int32 /*Material*/, const FModelMaterialColors& /*Colors*/ ) { return false; }
	// メッシュ・フレームを描くか（フレームを隠すと、その子のフレームのメッシュも描かない）
	virtual bool GetModelMeshVisible( int32 /*Model*/, int32 /*Mesh*/, bool& /*bOut*/ ) { return false; }
	virtual bool SetModelMeshVisible( int32 /*Model*/, int32 /*Mesh*/, bool /*bVisible*/ ) { return false; }
	virtual bool GetModelFrameVisible( int32 /*Model*/, int32 /*Frame*/, bool& /*bOut*/ ) { return false; }
	virtual bool SetModelFrameVisible( int32 /*Model*/, int32 /*Frame*/, bool /*bVisible*/ ) { return false; }
	// モデル全体の不透明度（0〜1。材質の不透明度に掛ける）
	virtual bool GetModelOpacity( int32 /*Model*/, float32& /*Out*/ ) { return false; }
	virtual bool SetModelOpacity( int32 /*Model*/, float32 /*Opacity*/ ) { return false; }
	// フレームの、いまの行列（最後に描いたときのアニメーションと置き場所で決まる）
	virtual bool GetModelFrameMatrix( int32 /*Model*/, int32 /*Frame*/, EModelFrameSpace /*Space*/, FMatrix4x4& /*Out*/ ) { return false; }
	// 有効なmodel/frameならtrueで追跡状態を返し、未設定時はfalse flagとして行列を保つ。失敗時は両Outを保つ。
	virtual bool GetModelFrameLocalMatrixOverride( int32 /*Model*/, int32 /*Frame*/, bool& /*bOutOverridden*/, FMatrix4x4& /*Out*/ ) { return false; }
	// 指定frameの完全な親基準local poseを上書きする。未対応・未準備・不正入力はfalseで既存状態を変えない。
	virtual bool SetModelFrameLocalMatrixOverride( int32 /*Model*/, int32 /*Frame*/, const FMatrix4x4& /*LocalToParent*/ ) { return false; }
	// 指定frameのlocal pose上書きを解除し、現在のanimation評価へ戻す。未対応・未準備はfalseで状態を変えない。
	virtual bool ClearModelFrameLocalMatrixOverride( int32 /*Model*/, int32 /*Frame*/ ) { return false; }
	// 形状（モーフ）の効き具合（0 = 効かない、1 = すべて効く。いまの値）
	virtual bool GetModelShapeRate( int32 /*Model*/, int32 /*Shape*/, float32& /*Out*/ ) { return false; }
	virtual bool SetModelShapeRate( int32 /*Model*/, int32 /*Shape*/, float32 /*Rate*/ ) { return false; }

	// 当たり判定。モデルの手元の座標（置き場所を含まない）で、準備したときの形（アニメーション・形状を含む）を写しておき、それに問い合わせる。
	// 形を写す（2 回目からは、写し直す）
	virtual bool PrepareModelCollision( int32 /*Model*/ ) { return false; }
	// From から To への線が、最初に当たる三角形（手元の座標。準備していなければ準備する）。当たらなければ false
	virtual bool RaycastModel( int32 /*Model*/, const FVector3& /*From*/, const FVector3& /*To*/, FModelHit& /*Out*/ ) { return false; }
	// 球と重なる三角形をすべて（手元の座標。準備していなければ準備する）。対応していなければ false
	virtual bool OverlapModelSphere( int32 /*Model*/, const FVector3& /*Center*/, float32 /*Radius*/, TArray<FModelHit>& /*Out*/ ) { return false; }
};
