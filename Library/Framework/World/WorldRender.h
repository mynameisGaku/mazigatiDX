#pragma once
#include <functional>

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Render/Billboard.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// ワールドの部品を、描く形（Framework の描画データ）にする
//   実行時の設置（WorldBuiltinSpawners.cpp）も、エディタのビューポートも、同じ関数で作る。
//   エディタで見えるものと、ゲームで見えるものが食い違わないようにするため。
//=============================================================================
namespace NWorld
{
	EDrawQuadOrient ToQuadOrient( EWorldBillboardMode Mode );

	// 定義のビルボードから、描く FBillboard を作る。Texture は読み込み済みのもの。
	// 画像の一部（bUseRect）を使えなかったとき（画像の大きさがまだ分からない・矩形が空）は、全体を使って OutRectApplied を false にする
	FBillboard MakeBillboard( const FWorldBillboard& Spec, const TSharedPtr<CTexture>& Texture, bool* OutRectApplied = nullptr );

	// 編集メッシュから、描く FMeshRenderer を作る（描く頂点・インデックスを作って、バッファにし、材質ごとの範囲・色・画像を付ける）。
	// LoadTexture は、材質の画像のファイル（空でないときだけ）から、画像を読み込む関数（読み込めなければ、空のポインタを返す）。nullptr でもよい（色だけで描く）。
	// 使える面が無い・バッファが作れない（バックエンドが動いていない）ときは、結果の Buffer が空（IsValid() が false）
	FMeshRenderer MakeMeshRenderer( const FEditMesh& Mesh, const std::function<TSharedPtr<CTexture>( const FString& )>& LoadTexture );
}
