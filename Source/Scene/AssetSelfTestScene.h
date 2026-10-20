#pragma once
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Scene/Scene.h"

//=============================================================================
// CAssetSelfTestScene
//   アセット・アニメーション・オーディオ・フォントの確認をするシーン。結果はログに出る。
//
//   実行: 起動引数 --selftest=assets（このシーンから始まり、終わると自動で終了する）。デバッグ窓のシーン一覧から選んでもよい。
//   Tools/RunSelfTests.ps1 -Suite assets で実行できる。
//
//   確かめること:
//     1. 純粋な計算（DxLib なし）: アニメーションの時間軸・出来事・スプライトシートのコマ割り・スプライトのアニメーター・
//        モデルのクロスフェード・オーディオのフェード / 距離減衰 / パン / バス / 数の制限 / 音楽の入れ替え・一時停止
//     2. アセットの管理（読み込みを偽物にして）: 共有 / 解放 / 別スレッドで手放した解放 / 非同期 / フォントファイルの登録
//     3. シーンでの動作（偽物のバックエンドで）: ECS に付けたアニメーターの自動更新・描画、音源コンポーネントの追従と停止
//     4. 本物の DxLib: 画像の読み込み、スプライトシートのコマ割りどおりのピクセル（反転・拡大・色）、フォントの描画と位置揃え、
//        サウンドの読み込み・再生・音量・パン・ピッチ、モデル（アニメーション付きの .x を生成して）
//=============================================================================
class CAssetSelfTestScene : public CScene
{
public:
	CAssetSelfTestScene();
	~CAssetSelfTestScene() override;

	// 起動引数に --selftest=assets があるか
	static bool IsRequestedOnCommandLine();

protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnExit() override;

private:
	// 本物の DxLib での確認（画像・スプライトのピクセル・フォント・モデル・サウンド）
	void RunRealChecks();
	// 本物のサウンドが鳴り終わるのを待って、結果を出す
	void FinishRealSound();

	struct FState;
	TUniquePtr<FState> State;
};
