#pragma once
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "Library/Framework/World/WorldEdit.h"

//=============================================================================
// 当たり判定を、モデルに合わせて作る（エディタの補助）
//   モデルを置いたあと、「そのモデルの大きさの箱」「球」「カプセル」「三角形そのまま」の当たり判定を、1 回の操作で付ける。
//   純粋な計算と編集の操作（ImGui を使わない）。モデルの大きさは IEditorAssetInfo 越し。
//
//   当たり判定の形は、オブジェクトのローカル座標（拡大は、実行時に掛かる）で、モデルの箱と同じ座標。
//=============================================================================
// 当たり判定の見せ方（ビューポートの色分けと凡例の元）。見るだけで、「効いているか・なぜか」が分かるように分ける
enum class EEditorColliderDisplay : uint8
{
	Solid,         // ぶつかる（既定、またはタグの規則で）
	SolidForced,   // ぶつかる（オブジェクト自身が On にしている）
	Trigger,       // 重なりを知らせるだけ（ぶつからない）
	Off,           // 剛体にならない（タグの規則で）
	OffForced,     // 剛体にならない（オブジェクト自身が Off にしている）
};

namespace NWorldEditor
{
	// Collider を持つオブジェクトの、見せ方（持たなければ Off）
	EEditorColliderDisplay ClassifyColliderDisplay( const FWorldSettings& Settings, const FWorldObject& Object );
	const char* GetColliderDisplayLabel( EEditorColliderDisplay Display );

	// モデルの箱（ローカルの Min..Max）に合う Collider。Base の Trigger などは引き継ぐ。
	//   Box    : 箱の大きさと中心（Offset）
	//   Sphere : 半径 = 箱のいちばん長い辺の半分、中心（Offset）= 箱の中心
	//   Capsule: 半径 = 横（X と Z）の長い辺の半分、高さ = 箱の高さ（半径の 2 倍より低ければ 2 倍）、中心 = 箱の中心
	//   Mesh   : Mesh（ファイル）は変えない（空なら、Model のファイルから作る）。大きさは使わない
	FWorldCollider FitColliderToBounds( EWorldColliderShape Shape, const FVector3& Min, const FVector3& Max, const FWorldCollider& Base = FWorldCollider() );

	// オブジェクトのモデル（Mesh の当たり判定なら、Mesh のファイル）に合わせて、Collider を作る（すでにあれば、形だけ変える）。
	// モデルが無い・大きさが分からないときは false（何も変えない）。Shape が Mesh のときは、大きさを使わないので、Model のファイルがあればよい
	bool FitColliderToModel( TWorldDocument& Document, int32 ObjectId, EWorldColliderShape Shape, const IEditorAssetInfo& Assets );
}
