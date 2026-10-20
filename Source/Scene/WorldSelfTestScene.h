#pragma once
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"

//=============================================================================
// CWorldSelfTestScene
//   ワールド（.world）の確認をするシーン。結果はログに出る。物理のあるシーン（IPhysicsScene）。
//
//   実行: 起動引数 --selftest=world（このシーンから始まり、終わると自動で終了する）。デバッグ窓のシーン一覧から選んでもよい。
//   Tools/RunSelfTests.ps1 -Suite world で実行できる。
//
//   確かめること:
//     1. 純粋な部分（DxLib もシーンもなし。WorldSelfTestLogic.cpp）: 定義と JSON（書き戻しで失われないこと・読み込みでの修復）・階層・
//        位置と回転・当たり判定を付けるかどうかの決定（優先順位）・検証・編集（Id の割り当て・親の付け替え・複製・取り消し）
//     2. アセットとして（偽のバックエンドで）: 保存・読み込み・共有・キャッシュの入れ替え・検証用のアセットの窓口
//     3. シーンへの設置（偽のバックエンドで）: 位置と親子・タグでの検索・タグへの結び付け・使えない部品の警告・消えたものの扱い・
//        モデル / ビルボード / 環境音の部品・描画コマンド・物理の剛体（本物の物理）: 規則に従って付く / 付かない、実際に球が乗る / すり抜ける
//     4. 本物の DxLib: ビルボードのピクセル（カメラの方を向く / Y 軸だけ向く / 向き固定・反転・矩形・基準点・色・不透明度・拡大）
//=============================================================================
class CWorldSelfTestScene : public CScene, public IPhysicsScene
{
public:
	CWorldSelfTestScene();
	~CWorldSelfTestScene() override;

	// 起動引数に --selftest=world があるか
	static bool IsRequestedOnCommandLine();

protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnExit() override;

private:
	// 本物の DxLib での確認（ビルボードのピクセル）
	void RunRealChecks();
	void Finish();

	struct FState;
	TUniquePtr<FState> State;
};
