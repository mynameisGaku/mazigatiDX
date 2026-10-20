#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Import/ImportedScene.h"

//=============================================================================
// DirectX の .x ファイル（テキスト形式）への書き出し
//   DxLib が読める .x に FImportedScene を書き出す（フレーム階層・メッシュ・法線・UV・マテリアル・スキン・アニメーション）。
//   .mv1 への変換の途中で使う（x -> DxLib で読む -> .mv1 に保存）。
//
//   FString text = NXFile::Write( Scene, TexturePaths );
//
//   ・同じ入力からは、同じ文字列ができる（確認しやすい）
//   ・ノードごとのフレーム名は NImportedScene::MakeFrameNames と同じ（骨の名前としてアニメーションが指す）
//   ・アニメーションのキーの時間は、キーの番号（毎秒 Fps コマ。DxLib での長さ = コマ数）。複数の Fps が混ざっていてもそのまま書く
//   ・スキンのあるメッシュは、骨の動きだけで動くので、単位行列のフレームを作ってそこに入れる
//=============================================================================
namespace NXFile
{
	// TexturePaths: Scene.Textures と同じ並びの、TextureFilename に書くパス（.x のあるフォルダからの相対がよい）。足りなければ Scene.Textures[i].File
	FString Write( const FImportedScene& Scene, const TArray<FString>& TexturePaths );
}
