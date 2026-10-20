#pragma once
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Scene/Scene.h"

//=============================================================================
// CEcsSelfTestScene
//   ECS 化した CTransformComponent のワールド値が、以前の計算（親のコンポーネントをたどる再帰）と
//   1 ビットも違わないことを、ランダムな階層に対して確かめるシーン。結果はログに出る。
//
//   実行: 起動引数 --selftest=ecs（このシーンから始まり、終わると自動で終了する）。デバッグ窓のシーン一覧から選んでもよい。
//   Tools/RunSelfTests.ps1 は、ワーカーの数を変えて何度も実行し、結果（ハッシュ）が一致することも確かめる。
//
//   確かめること（30 フレーム、数万個のオブジェクト）:
//     - 値を変える / 親を付け替える / オブジェクトやコンポーネントを破棄する / 新しく生成する、をランダムに行う
//     - そのたびに、全オブジェクトのワールド値を 2 通りで読んで、以前の計算と比べる
//         1) 描画前の一括計算を待たずに読む（古ければ、その場で計算される）
//         2) ジョブシステムで一斉に計算してから読む
//     - 全ワールド値のハッシュを出す。ワーカーの数が違っても同じ値になる（実行の順番に結果が依存しない）
//
//   続けて、システムと描画システムも確かめる（詳しくは EcsSelfTestScene.cpp）:
//     - システム: 並列に動かした結果が、1 つずつ順番に行った結果と 1 ビットも違わない
//     - 描画システム: 記録された描画コマンドが、描画順に素直に作ったものと 1 ワードも違わず、
//       DxLib で実際に描いた画面（コマンド経由）が、DxLib を直接呼んだ画面と 1 ピクセルも違わない（2D・3D）
//=============================================================================
class CEcsSelfTestScene : public CScene
{
public:
	CEcsSelfTestScene();
	~CEcsSelfTestScene() override;

	// 起動引数に --selftest=ecs があるか
	static bool IsRequestedOnCommandLine();

protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnExit() override;

private:
	// 自己確認のあとに、全オブジェクトを一斉に dirty にして Update の時間を測る（結果はログに出るだけ。合否には関係しない）
	void RunBenchmark();
	// 描画システム（記録は並列、DxLib への実行は直列）と、ふつうの Draw() の時間を比べる（同じく、ログに出るだけ）
	void RunDrawBenchmark();

	struct FState;
	TUniquePtr<FState> State;
};
