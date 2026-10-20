#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldValidate.h"

//=============================================================================
// エディタが見せる・選ぶための、オブジェクトの形
//   モデルの大きさ（箱）、ビルボードの板、当たり判定の形、音や空のオブジェクトの目印を、ワールドでの図形にする。
//   ピッキング（クリックで選ぶ）、選んだものの枠線、「選んだものに寄る」が、同じ図形を使う。
//   純粋な計算（DxLib・ImGui を使わない）。アセットの大きさは IEditorAssetInfo 越しに問い合わせる（偽物で確かめられる）。
//
//   モデルは、メッシュ全体を囲む箱（読み込んだ大きさ）で選ぶ。三角形までは見ない（細かい選択は、後の段階）。
//=============================================================================
// 当たり判定に使う、モデルの三角形（モデルのローカル座標。Indices は 3 つで 1 三角形）
struct FEditorCollisionMesh
{
	TArray<FVector3> Vertices;
	TArray<uint32> Indices;

	int32 GetNumTriangles() const { return Indices.Num() / 3; }
};

class IEditorAssetInfo : public IWorldAssetQuery
{
public:
	// モデルのローカルでの大きさ（メッシュ全体を囲む箱）。読み込めない・メッシュが無いなら false
	virtual bool GetModelBounds( const FString& Path, FVector3& OutMin, FVector3& OutMax ) const = 0;
	// 画像のピクセルの大きさ。読み込めないなら false
	virtual bool GetTextureSize( const FString& Path, int32& OutWidth, int32& OutHeight ) const = 0;
	// モデルの三角形（メッシュの当たり判定の枠線を出すため）。読み込めない・分からないなら nullptr（返したものは、次に Clear されるまで有効）
	virtual const FEditorCollisionMesh* GetCollisionMesh( const FString& /*Path*/ ) const { return nullptr; }
};

enum class EEditorShapeKind : uint8
{
	Box,       // 中心・回転・半分の長さ
	Sphere,    // 中心・半径
	Capsule,   // 線分 A-B と半径
	Quad,      // 中心と、横・縦の半分のベクトル（板）
};

// 何を表す図形か（表示の色や、選ぶかどうかの切り替えに使う）
enum class EEditorShapeRole : uint8
{
	Model,
	Billboard,
	Sound,
	Effect,    // エフェクト（粒子など）の目印。位置だけ（Sound と同じ扱い）
	Collider,
	Marker,    // 見えるものが何も無いオブジェクトの目印（常に画面で一定の大きさ）
};

struct FEditorShape
{
	EEditorShapeKind Kind = EEditorShapeKind::Box;
	EEditorShapeRole Role = EEditorShapeRole::Marker;

	FVector3 Center = FVector3::Zero;            // Box / Sphere / Quad
	FQuaternion Rotation = FQuaternion::Identity;   // Box
	FVector3 Half = FVector3::Zero;              // Box: 各軸の半分の長さ
	float32 Radius = 0.0f;                       // Sphere / Capsule
	FVector3 A = FVector3::Zero;                 // Capsule の両端
	FVector3 B = FVector3::Zero;
	FVector3 HalfRight = FVector3::Zero;         // Quad
	FVector3 HalfUp = FVector3::Zero;
	bool bApproximate = false;                   // 大きさが分からず、仮の大きさにしている（モデルの箱を読めなかったとき）
};

// 図形を作るときの周りの情報。Camera が nullptr なら、ビルボードは向きを固定として、目印は固定の大きさにする
struct FEditorShapeContext
{
	const IEditorAssetInfo* Assets = nullptr;
	const FEditorCamera* Camera = nullptr;
	FEditorViewport Viewport;
	float32 MarkerPixels = 9.0f;   // 目印の半径（ピクセル）
};

namespace NWorldEditor
{
	// 1 つのオブジェクト自身の図形（子は含まない）。WorldTransform は、そのオブジェクトのワールドでの姿勢
	TArray<FEditorShape> GatherShapes( const FWorldObject& Object, const FTransform& WorldTransform, const FEditorShapeContext& Context );

	// 図形を囲む箱（軸に平行）。図形が無ければ false
	bool ComputeShapesBounds( const TArray<FEditorShape>& Shapes, FVector3& OutMin, FVector3& OutMax );
	// 図形の 1 点（光線との交差の確認や、矩形選択に使う、代表の点）
	FVector3 GetShapeCenter( const FEditorShape& Shape );

	// ビルボードの板の向き（描くときの実行側と同じ決め方）。CameraRight / CameraUp は、カメラの横・上
	//   Camera: カメラの向きに沿う（オブジェクトの Roll が視線まわりの回転）、CameraY: Y 軸だけ回ってカメラの方を向く、World: オブジェクトの向き
	void ComputeBillboardAxes( EWorldBillboardMode Mode, const FTransform& WorldTransform, const FVector3& CameraEye, const FVector3& CameraRight, const FVector3& CameraUp,
							   FVector3& OutRight, FVector3& OutUp );

	// 図形の輪郭を、線分の列（2 点ずつ）として OutLines に足す。箱 12 本、球は 3 本の大円、カプセルは両端の円と半円と 4 本の筋、板は 4 本。
	// 選んだものの枠・当たり判定の表示が使う。CircleSegments は円 1 周の分割数
	void AppendShapeWireLines( const FEditorShape& Shape, int32 CircleSegments, TArray<FVector3>& OutLines );

	// メッシュの当たり判定の枠線（辺。共有している辺は 1 本）を、OutLines に足す（Transform はモデル -> ワールド）。
	// 三角形が MaxTriangles より多いときは、先頭の MaxTriangles 個だけにして false を返す（重くならないように）
	bool AppendMeshWireLines( const FEditorCollisionMesh& Mesh, const FTransform& Transform, int32 MaxTriangles, TArray<FVector3>& OutLines );
	// 編集メッシュの、面の縁（共有している縁は 1 本。使える面だけ）を、OutLines に足す（Transform はローカル -> ワールド）。
	// 面が MaxFaces より多いときは、先頭の MaxFaces 個だけにして false を返す
	bool AppendEditMeshWireLines( const FEditMesh& Mesh, const FTransform& Transform, int32 MaxFaces, TArray<FVector3>& OutLines );

	// 当たり判定の形を、ワールドの図形にする（拡大は大きさに掛ける。球・カプセルの半径は、拡大の大きいほうに合わせた近似）
	FEditorShape MakeColliderShape( const FWorldCollider& Collider, const FTransform& WorldTransform );
}
