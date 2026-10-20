#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Framework/World/Editor/EditorCollider.h"
#include "Library/Framework/World/Editor/EditorGizmo.h"
#include "Library/Framework/World/Editor/EditorMeshPicking.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/WorldEdit.h"

//=============================================================================
// ワールドエディタの「編集の状態」
//   編集中のワールド（元に戻す履歴つき）、選択、カメラ、ギズモの設定とドラッグ、コピーしたもの。
//   エディタの窓（ImGui）が持つのは、この状態と、画面だけの状態（窓の開閉・入力欄など）。
//   「クリックしたら何が選ばれるか」「ギズモを動かすとワールドがどう変わるか」「削除したあと選択がどうなるか」は、
//   ここ（純粋なコード。DxLib・ImGui を使わない）で決めて、画面なしでテストする。
//
//   守ること（不変条件）:
//   - 選択は、いつもワールドに存在するオブジェクトだけ（消した・元に戻して無くなったものは外れる）。
//   - 操作は、1 回の「元に戻す」にまとまる（複数のオブジェクトの削除・複製・貼り付け・ギズモの 1 回のドラッグ）。
//   - 編集の操作は NWorldEdit（WorldEdit.h）を使う。Document を直接編集したあとは、SyncSelection() を呼ぶ。
//=============================================================================
class CWorldEditorSession
{
public:
	enum class ESelectMode : uint8
	{
		Replace,   // これだけを選ぶ
		Add,       // 加える（すでに選んでいれば、主な選択にする）
		Toggle,    // 選んでいれば外し、なければ加える
		Remove,    // 外す
	};

	//-------------------------------------------------------------------------
	// 文書とカメラ
	//-------------------------------------------------------------------------
	TWorldDocument Document;
	FString FilePath;                // 保存先（アセットのルートからの相対）。空なら、まだ保存していない新規
	FEditorCamera Camera;

	// ワールドを入れ替える（履歴・選択・ドラッグは捨てる。コピーしたものは残る: 別のワールドへ貼れる）
	void Reset( const FWorldDefinition& World, const FString& InFilePath );

	//-------------------------------------------------------------------------
	// 選択
	//-------------------------------------------------------------------------
	const TArray<int32>& GetSelection() const { return Selected; }
	// 主な選択（最後に選んだもの。インスペクターで見せる、ギズモの向きを決める）。選択が空なら WorldNoId
	int32 GetPrimary() const { return Selected.IsEmpty() ? WorldNoId : Selected.Last(); }
	bool IsSelected( int32 ObjectId ) const { return Selected.Contains( ObjectId ); }
	// 選択のうち、祖先も選んでいるものを除いた根（まとめて動かす・消す・複製するときの対象）
	TArray<int32> GetSelectedRoots() const;

	void Select( int32 ObjectId, ESelectMode Mode = ESelectMode::Replace );
	void SetSelection( const TArray<int32>& Ids, ESelectMode Mode = ESelectMode::Replace );
	void ClearSelection();
	void SelectAll();
	// 存在しないものを選択から外す
	void SyncSelection();

	// クリックで選ぶ。当たったオブジェクトの Id を返す（何にも当たらなければ WorldNoId。Replace なら選択を空にする）。
	// bCycle: すでに選んでいるものに当たったら、重なっている次のものを選ぶ（同じ場所を続けてクリックしたとき）
	int32 ClickSelect( const FEditorRay& Ray, const FEditorShapeContext& Context, ESelectMode Mode, bool bCycle = false );
	// タグを持つオブジェクトすべて（無効なものも）を選ぶ。コードがタグで探すのと同じ（大文字小文字を区別する）。一致した数を返す
	int32 SelectByTag( const FString& Tag, ESelectMode Mode = ESelectMode::Replace );
	// 矩形（ビューポートのピクセル）で選ぶ
	void RectSelect( const FEditorShapeContext& Context, float32 X0, float32 Y0, float32 X1, float32 Y1, ESelectMode Mode );

	//-------------------------------------------------------------------------
	// 編集（どれも 1 回の「元に戻す」。選択を直す）
	//-------------------------------------------------------------------------
	bool Undo();
	bool Redo();

	int32 DeleteSelection();                         // 消した数
	TArray<int32> DuplicateSelection();              // 複製を選んで、その Id を返す
	void CopySelection();
	bool CanPaste() const { return !Clipboard.IsEmpty(); }
	TArray<int32> Paste( const FVector3& WorldOffset = FVector3::Zero, int32 ParentId = WorldNoId );   // 貼ったものを選ぶ
	int32 GroupSelection( const FString& Name = "Group" );   // 作った親を選ぶ
	bool UngroupSelection();                         // 部品を持たない親だけ。外した数が 0 なら false
	bool MoveSelection( int32 TargetId, NWorldEdit::EWorldDropPosition Position );   // アウトライナーのドラッグ

	// 選んだオブジェクトの当たり判定を、それぞれのモデルに合わせて付ける（すでにあれば、形を替える）。1 回の「元に戻す」。付けた数を返す
	int32 FitSelectionColliders( EWorldColliderShape Shape, const IEditorAssetInfo& Assets );

	// 作って、選ぶ。作った Id を返す
	int32 CreateMesh( EEditMeshPrimitive Kind, const FVector3& WorldPosition, int32 ParentId = WorldNoId, bool bCollider = true );
	// 選んだオブジェクトのうち、Model 部品を編集メッシュに変えられたものの数（1 回の「元に戻す」）。変えた物の三角形は IEditorAssetInfo から取る
	int32 ConvertSelectionToMeshes( const IEditorAssetInfo& Assets );
	// 選んだオブジェクト（根と子孫）を、PrefabPath の Prefab 部品を持つ 1 つのオブジェクトに置き換えて、それを選ぶ（取り出した定義の保存は、呼び出し側）。置いた Id を返す。1 回の「元に戻す」
	int32 ReplaceSelectionWithPrefab( const FString& PrefabPath, const FVector3& Origin, const FString& Name );
	// Ids の Prefab 部品を持つオブジェクトを、中身に展開する（NWorldEdit::UnpackPrefab）。中身が読めないものは飛ばす。展開できた数を返す。1 回の「元に戻す」
	int32 UnpackPrefabs( const TArray<int32>& Ids, const IEditorAssetInfo& Assets );
	int32 CreateEmpty( const FVector3& WorldPosition, const FString& Name = "Empty", int32 ParentId = WorldNoId );
	int32 CreateAsset( const FString& AssetPath, const FVector3& WorldPosition, const IEditorAssetInfo* Info, int32 ParentId = WorldNoId );
	int32 CreateCollider( EWorldColliderShape Shape, bool bTrigger, const FVector3& WorldPosition, int32 ParentId = WorldNoId );

	// 選択に寄る。選択が空・図形が無ければ false
	bool FocusSelection( const FEditorViewport& Viewport, const IEditorAssetInfo* Assets );

	//-------------------------------------------------------------------------
	// ギズモ
	//-------------------------------------------------------------------------
	EEditorGizmoMode GizmoMode = EEditorGizmoMode::Translate;
	EEditorGizmoSpace GizmoSpace = EEditorGizmoSpace::World;
	bool bSnap = false;
	float32 MoveSnap = 0.5f;
	float32 RotateSnapDegrees = 15.0f;
	float32 ScaleSnap = 0.1f;

	// ギズモの位置と向き: 位置は、選んだ根の位置の中心（1 つなら、そのオブジェクト）。Local のときの向きは、主な選択の向き。選択が空なら false
	bool GetGizmoFrame( const FEditorViewport& Viewport, FEditorGizmoFrame& OutFrame ) const;

	// ドラッグ: 取っ手をつかむ → 動かす（毎フレーム）→ 離す / 取り消す。動かしている間は、毎フレームの履歴を積まず、離すまでで 1 回。
	// 動かさないまま離したら、履歴は積まない。Cancel は、ドラッグでの変更を元に戻して、やり直しにも残さない
	bool BeginGizmoDrag( const FEditorViewport& Viewport, EEditorGizmoHandle Handle, const FEditorRay& Ray );
	// bInvertSnap: 一時的に、スナップの有効 / 無効を逆にする（Ctrl を押している間など）
	void UpdateGizmoDrag( const FEditorRay& Ray, bool bInvertSnap = false );
	void EndGizmoDrag();
	void CancelGizmoDrag();
	bool IsGizmoDragging() const { return Drag.IsActive(); }
	const FEditorGizmoDrag& GetGizmoDrag() const { return Drag; }
	// ドラッグ中の、開始からの変化量（最後の UpdateGizmoDrag の結果。画面に数値を出すため）
	const FEditorGizmoDelta& GetGizmoDelta() const { return LastDelta; }

	const NWorldEdit::FWorldClipboard& GetClipboard() const { return Clipboard; }

	//-------------------------------------------------------------------------
	// 編集メッシュの編集（Mesh 部品を持つ、1 つのオブジェクトの、頂点・辺・面）
	//   編集の間、ギズモ・ドラッグは、選んだ頂点（辺・面の頂点）を動かす。選択は、メッシュの要素（頂点・辺・面の番号）。
	//   操作はどれも、1 回の「元に戻す」。頂点の数が変わる操作（消す・つなぐ）のあとは、選択を選び直す（番号が詰まるため）。
	//   Undo / Redo のあと、メッシュが変わった・無くなったときは、選択が直る・編集が終わる（SyncSelection）。
	//-------------------------------------------------------------------------
	// 編集を始める。オブジェクトが、Mesh 部品を持つなら true（オブジェクトを選んだ状態にする）。ドラッグの途中は始められない
	bool BeginMeshEdit( int32 ObjectId );
	void EndMeshEdit();
	bool IsMeshEditing() const { return MeshObjectId != WorldNoId; }
	int32 GetMeshObjectId() const { return MeshObjectId; }
	NEditMesh::EEditMeshElement GetMeshElement() const { return MeshElement; }
	// 選ぶ要素の種類を替える（選択は、新しい種類に変える）
	void SetMeshElement( NEditMesh::EEditMeshElement Element );
	const FEditMeshSelection& GetMeshSelection() const { return MeshSelection; }
	// 選択を、そのまま置き換える（メッシュに無い番号は外す。選ぶ要素の種類は、呼ぶ側が合わせる）
	void SetMeshSelection( const FEditMeshSelection& Selection );
	// 編集しているメッシュ（編集中でなければ nullptr）と、そのワールドでの姿勢
	const FEditMesh* GetEditedMesh() const;
	FTransform GetEditedWorldTransform() const;

	// 選ぶ: クリック（ビューポートのピクセル。当たれば true）・矩形・全部・無し・反転・つながった面
	bool MeshClickSelect( const FEditorViewport& Viewport, float32 X, float32 Y, ESelectMode Mode );
	void MeshRectSelect( const FEditorViewport& Viewport, float32 X0, float32 Y0, float32 X1, float32 Y1, ESelectMode Mode, bool bOnlyVisible = true );
	void MeshSelectAll();
	void MeshSelectNone();
	void MeshInvertSelection();
	void MeshSelectLinked();

	// 操作（変わったなら true）。面を使う操作は、選んでいるものから、頂点がすべて選ばれた面を使う（種類が頂点・辺でも）
	bool MeshExtrude( float32 Distance, bool bIndividual = false );   // 押し出した面を選ぶ（面の選択にする）
	bool MeshInset( float32 Amount );                                 // 押し出した（中の）面を選ぶ
	bool MeshDeleteSelection();                                       // 面: その面。頂点: その頂点を使う面と頂点。辺: その辺を使う面
	bool MeshFlip();
	bool MeshMerge();                                                 // 選んだ頂点を 1 つに（平均の位置）。つないだ頂点を選ぶ
	bool MeshWeld( float32 Distance );                                // 選んだ頂点（無ければ、全体）で、近いものをつなぐ
	bool MeshSubdivide();
	bool MeshTriangulate();
	bool MeshSetMaterial( int32 Material );                           // 選んだ面の材質
	// 基本の形を、このメッシュに足す（ワールドの位置に。足した面を選ぶ）
	bool MeshAppendPrimitive( EEditMeshPrimitive Kind, const FVector3& WorldPosition );

private:
	float32 GetActiveSnap( bool bInvert ) const;
	void SelectNew( const TArray<int32>& Ids );

	// 編集メッシュ
	void SyncMeshEdit();
	void ApplyMeshSelection( const FEditMeshSelection& Picked, ESelectMode Mode );
	TArray<int32> GetMeshFaceSelection() const;
	// 編集中のメッシュのコピーに Op を掛け、変わっていれば 1 回の編集として書き戻す。Op( Mesh, Selection ) は、メッシュと選択を直して、何か変えたなら true を返す
	template<typename TOp>
	bool ApplyMeshOp( TOp&& Op );

	int32 MeshObjectId = WorldNoId;
	NEditMesh::EEditMeshElement MeshElement = NEditMesh::EEditMeshElement::Vertex;
	FEditMeshSelection MeshSelection;
	// 頂点のドラッグ: 動かす頂点と、開始のときの、ワールドでの位置
	TArray<int32> MeshDragVertices;
	TArray<FVector3> MeshDragStart;
	FTransform MeshDragWorld;

	TArray<int32> Selected;
	NWorldEdit::FWorldClipboard Clipboard;

	// ギズモのドラッグ
	struct FDragObject
	{
		int32 ObjectId = WorldNoId;
		FTransform StartWorld;
	};
	FEditorGizmoDrag Drag;
	FEditorGizmoDelta LastDelta;
	FEditorGizmoFrame DragFrame;
	TArray<FDragObject> DragObjects;
	bool bDragEdited = false;
};
