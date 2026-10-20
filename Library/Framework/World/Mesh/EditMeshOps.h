#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Math/Quaternion.h"
#include "Library/Math/Transform.h"

//=============================================================================
// 編集メッシュの編集操作（純粋な関数。DxLib・シーン・ImGui を使わない）
//   エディタの画面は、これを呼ぶだけ。選んだもの（頂点・辺・面）を動かす・押し出す・面を差す（インセット）・消す・つなぐ・割る、を画面なしでテストできる。
//
//   - 操作は FEditMesh を直接書き換える。頂点の数が減る操作（削除・結合）は、使われない頂点を取り除いて、番号を詰める（そのあと、選択の番号は無効になるので、選び直す）。
//   - 面の表・裏は変えない（表の向きは、操作の前後で同じ側）。閉じた形は、操作のあとも閉じている（削除を除く）。
//   - 面・頂点の番号は、操作のたびに有効なものを渡すこと（範囲外・重複は、読み飛ばす）。
//=============================================================================

// 辺は、頂点の番号の組（小さい方が先）。辞書の鍵にするため、1 つの数にする
namespace NEditMesh
{
	int64 MakeEdgeKey( int32 A, int32 B );
	void SplitEdgeKey( int64 Key, int32& OutA, int32& OutB );
}

// 選択（頂点・辺・面）。エディタの選択の種類（頂点 / 辺 / 面）に従って、1 つの集合だけが使われる。
// どの集合も、小さい順で、重複がない
struct FEditMeshSelection
{
	TArray<int32> Vertices;
	TArray<int64> Edges;      // NEditMesh::MakeEdgeKey
	TArray<int32> Faces;

	bool IsEmpty() const { return Vertices.IsEmpty() && Edges.IsEmpty() && Faces.IsEmpty(); }
	void Clear()
	{
		Vertices.Reset();
		Edges.Reset();
		Faces.Reset();
	}
};

namespace NEditMesh
{
	// 選ぶ要素の種類
	enum class EEditMeshElement : uint8
	{
		Vertex,
		Edge,
		Face,
	};

	// 集合に、小さい順を保って足す・外す・切り替える（重複しない）
	void AddSorted( TArray<int32>& Set, int32 Value );
	void AddSorted( TArray<int64>& Set, int64 Value );
	bool RemoveSorted( TArray<int32>& Set, int32 Value );
	bool RemoveSorted( TArray<int64>& Set, int64 Value );
	bool ContainsSorted( const TArray<int32>& Set, int32 Value );
	bool ContainsSorted( const TArray<int64>& Set, int64 Value );

	// 存在しない頂点・面・辺（辺は、どの面にも無い）を、選択から外す
	void CleanSelection( const FEditMesh& Mesh, FEditMeshSelection& Selection );
	// 選択を、別の種類の要素に変える（選んだ頂点を、そのまま動かす要素として引き継ぐ）:
	//   頂点へ: 選んでいるものの頂点すべて。辺へ: 両端が、選んだ頂点に入る辺。面へ: すべての頂点が入る面。
	// 戻り値は、新しい種類の集合だけが入った選択
	FEditMeshSelection ConvertSelection( const FEditMesh& Mesh, const FEditMeshSelection& Selection, EEditMeshElement To );

	//-------------------------------------------------------------------------
	// 問い合わせ
	//-------------------------------------------------------------------------
	// 選択が動かす頂点（選んだ頂点・辺の両端・面の頂点）。小さい順・重複なし・範囲内だけ
	TArray<int32> GetAffectedVertices( const FEditMesh& Mesh, const FEditMeshSelection& Selection );
	// それらの位置の平均
	FVector3 ComputeSelectionCenter( const FEditMesh& Mesh, const FEditMeshSelection& Selection );
	// 選んだ面の法線の、面積で重みを付けた平均（長さ 1）。面を選んでいない・打ち消し合うなら Zero
	FVector3 ComputeSelectionNormal( const FEditMesh& Mesh, const FEditMeshSelection& Selection );
	// すべての（使える）面の辺（向きのない組）。重複なし
	TArray<int64> GetAllEdges( const FEditMesh& Mesh );
	// 頂点を使う面
	TArray<int32> GetFacesUsingVertices( const FEditMesh& Mesh, const TArray<int32>& Vertices );
	// 辺を使う面（1 つか 2 つ。3 つ以上なら、それだけ）
	TArray<int32> GetFacesUsingEdge( const FEditMesh& Mesh, int64 Edge );
	// 面から、辺（共有している辺）でつながっている面すべて（Faces を含む）。形の島を選ぶのに使う
	TArray<int32> GetLinkedFaces( const FEditMesh& Mesh, const TArray<int32>& Faces );
	// 面の辺のうち、ほかの面と共有していない辺（開いている縁）の数
	int32 CountOpenEdges( const FEditMesh& Mesh );

	//-------------------------------------------------------------------------
	// 形を変える
	//-------------------------------------------------------------------------
	// 面を押し出す: 選んだ面（まとめて 1 つの塊として。bIndividual なら、1 枚ずつ）の縁に壁を作り、面を Distance だけ、面の法線（頂点では、選んだ面の法線の平均）の向きに動かす。
	// 面の番号は変わらない（押し出した面が、そのままの番号で、動いた先にある）。戻り値は、新しくできた頂点（選び直しに使う）と、押し出した面
	FEditMeshSelection ExtrudeFaces( FEditMesh& Mesh, const TArray<int32>& Faces, float32 Distance, bool bIndividual = false );
	// 面を差す（インセット）: 面ごとに、縁に枠を残して、中の面を中心へ縮める。Amount は 0..1（0 = 縮めない、1 = 点になる手前の最大。0.95 までにする）。押し出した面を返す
	FEditMeshSelection InsetFaces( FEditMesh& Mesh, const TArray<int32>& Faces, float32 Amount );
	// 面を消す（使われなくなった頂点も消える）。消した面の数
	int32 DeleteFaces( FEditMesh& Mesh, const TArray<int32>& Faces );
	// 頂点を消す（その頂点を使う面も消える）。消した面の数
	int32 DeleteVertices( FEditMesh& Mesh, const TArray<int32>& Vertices );
	// 面の表裏を逆にする（頂点の並びを逆にする）
	void FlipFaces( FEditMesh& Mesh, const TArray<int32>& Faces );
	// 頂点を 1 つにまとめる（位置は、それらの平均）。つぶれた面は消える。まとめた頂点の（詰めたあとの）番号。2 つ未満なら -1
	int32 MergeVertices( FEditMesh& Mesh, const TArray<int32>& Vertices );
	// Distance 以内の頂点どうしを、1 つにまとめる（Vertices が空なら、全体。番号の小さい方の位置を残す）。減った頂点の数
	int32 WeldVertices( FEditMesh& Mesh, const TArray<int32>& Vertices, float32 Distance );
	// 面を割る: 辺の真ん中と面の中心に頂点を足して、辺の数だけの四角形にする。隣の面には、辺の真ん中の頂点を足す（隙間ができないように）。増えた面の数
	int32 SubdivideFaces( FEditMesh& Mesh, const TArray<int32>& Faces );
	// 3 つより多い頂点の面を、三角形に分ける（凹んだ面の直し方）。増えた面の数
	int32 TriangulateFaces( FEditMesh& Mesh, const TArray<int32>& Faces );
	// どの面にも使われていない頂点を消して、番号を詰める。消した数
	int32 RemoveUnusedVertices( FEditMesh& Mesh );
	// 使えない面（頂点が 3 つ未満・範囲外・同じ頂点を 2 度）を消す。消した面の数
	int32 RemoveInvalidFaces( FEditMesh& Mesh );

	//-------------------------------------------------------------------------
	// 材質
	//-------------------------------------------------------------------------
	void SetFaceMaterial( FEditMesh& Mesh, const TArray<int32>& Faces, int32 Material );
	// 材質を足す。同じものがあれば、その番号。戻り値は番号
	int32 AddMaterial( FEditMesh& Mesh, const FEditMeshMaterial& Material );
	// 材質を消す: その材質の面は、材質 0（0 を消すなら、新しい 0）になり、後ろの番号は 1 つ前へ詰まる。範囲外なら false
	bool RemoveMaterial( FEditMesh& Mesh, int32 Index );
	bool IsSameMaterial( const FEditMeshMaterial& A, const FEditMeshMaterial& B );

	//-------------------------------------------------------------------------
	// 足す・作る
	//-------------------------------------------------------------------------
	// Source を、Transform で動かして、Destination に足す（頂点・面。材質は、同じものがあれば共有し、無ければ足す）。鏡写しの変換（負の拡大）では、面の表裏を保つ。
	// 足した面の番号の範囲（先頭）を返す
	int32 AppendMesh( FEditMesh& Destination, const FEditMesh& Source, const FTransform& Transform );
	// 三角形の並び（頂点の位置と、3 つで 1 つのインデックス）から、編集メッシュを作る。WeldDistance 以内の頂点は、1 つにまとめる（0 ならまとめない）。つぶれた三角形は捨てる。
	// 三角形の表は、(V1 - V0) x (V2 - V0) の側（DxLib の表）のまま
	FEditMesh MakeFromTriangles( TArrayView<const FVector3> Positions, TArrayView<const uint32> Indices, float32 WeldDistance );
}
