#pragma once
#include "AssetSelfTestSupport.h"
#include "Library/Core/Memory/SharedPtr.h"

class CTexture;
class CScene;
class FWorldSpawnerRegistry;

//=============================================================================
// ワールド（.world）の自己確認（CWorldSelfTestScene）の、ファイルをまたぐ宣言
//=============================================================================
namespace NWorldTest
{
	// 純粋な部分（定義と JSON・階層・当たり判定の決定・検証・編集。DxLib もシーンも使わない）の確認（WorldSelfTestLogic.cpp）
	void RunLogicTests( NAssetTest::FChecker& C );
	// 入れ子のワールド（Prefab 部品）の純粋な部分（JSON・検証・取り出す / 置き換える / 展開する。WorldPrefabSelfTest.cpp）
	void RunPrefabLogicTests( NAssetTest::FChecker& C );
	// 入れ子のワールドの設置（位置・階層・タグ・結び付け・物理・循環・深さ・片付け）。アセットを使えるフェーズで呼ぶ（WorldPrefabSelfTest.cpp）
	void RunPrefabSceneTests( NAssetTest::FChecker& C, ::CScene& Scene, ::CAssetManager& Assets, const ::FWorldSpawnerRegistry& Registry );
	// エフェクト（パーティクル）の定義（JSON・内容の識別・検証。WorldEffectSelfTestData.cpp）
	void RunEffectDataTests( NAssetTest::FChecker& C );
	// 粒子のシミュレーション・バックエンドの一覧・組み込みの丸の画像（WorldEffectSelfTestSim.cpp）
	void RunEffectSimTests( NAssetTest::FChecker& C );
	// エフェクト定義の編集・上限・履歴と未保存状態を確認する。
	void RunEffectEditingTests( NAssetTest::FChecker& C );
	// 別の ImGui 文脈で、文字・Tab・ボタン入力が実際の文書と履歴へ反映されることを確認する。
	void RunEffectEditorInputTests( NAssetTest::FChecker& C );
	// 別 ImGui 文脈から材質欄・寿命曲線へ入力し、Undo / Redo と不正値の修復を確認する。
	void RunEffectMaterialInputTests( NAssetTest::FChecker& C );
	// Appearance曲線の実panel入力、入力履歴境界、Add/Remove/Resetと不正値修復を確認する。
	void RunEffectAppearanceInputTests( NAssetTest::FChecker& C );
	// 実再生からAppearance倍率が材質・従来描画命令へ渡る値を確認する。
	void RunEffectAppearancePlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 実シミュレーション・Playback・描画記録を通し、Appearanceの色・幅・透明度を画素で確認する。
	void RunEffectAppearanceRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// 保存・別名編集・chunk分割と幅0/alpha0によるRibbon分断を再生で確認する。
	void RunEffectAppearanceBoundaryTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 定義編集画面のファイル操作、保存前の再生、描画先の寿命を実機で確認する。
	void RunEffectEditorRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// 帯と分岐雷の純粋な経路・保存・境界値を確認する。
	void RunEffectPathTests( NAssetTest::FChecker& C );
	// 実際の再生から帯の命令とプリセットの複製までを確認する。
	void RunEffectPathPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 帯の可変長の命令と実機ピクセルを確認する。
	void RunEffectRibbonRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor );
	// Tree Lightningの設定・枝形状・成長・放電を純粋計算で確認する。
	void RunEffectTreeLightningTests( NAssetTest::FChecker& C );
	// Tree Lightning path欄の種別選択、入力履歴、共通値と固有値の修復を確認する。
	void RunEffectTreeLightningInputTests( NAssetTest::FChecker& C );
	// Tree Lightningの本物の再生から材質帯・旧Ribbon命令・全体particle上限を確認する。
	void RunEffectTreeLightningPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 実simulationとDxLib描画を通し、Treeの成長・放電・正面と斜めの画素を確認する。
	void RunEffectTreeLightningRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// 材質曲線の保存、検証、Undo / Redo、寿命割合の純粋計算を確認する。
	void RunEffectMaterialTests( NAssetTest::FChecker& C );
	// UV歪み設定の保存値、JSON、hash、値域と互換を確認する。
	void RunEffectUVDistortionTests( NAssetTest::FChecker& C );
	// 帯のUVゆがみ命令の可変長payload、拒否条件、既存帯命令のbyte互換を確認する。
	void RunEffectRibbonUVDistortionCommandTests( NAssetTest::FChecker& C );
	// UV歪み付き材質帯を実再生し、点ごとの強度・時間・分割を確認する。
	void RunEffectRibbonUVDistortionPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 偽backendで帯のノイズ画像の共有・欠落・所有寿命を確認する。
	void RunEffectRibbonUVDistortionResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir );
	// 実GPUでUV歪み帯の穴・境界・既存描画への影響を確認する。
	void RunEffectRibbonUVDistortionRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// UV歪み欄の実入力、履歴と不正値の修復を確認する。
	void RunEffectUVDistortionInputTests( NAssetTest::FChecker& C );
	// 実再生でUV歪みの画像資源と粒子別強度を確認する。
	void RunEffectUVDistortionPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// UV歪み画像のpending・欠落・共有寿命とfake handleの解放を確認する。
	void RunEffectUVDistortionResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir );
	// 実GPUでUV歪み画像の参照位置と元の描画互換を確認する。
	void RunEffectUVDistortionRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// Appearanceの色倍率、範囲検証、JSONとhashの純粋計算を確認する。
	void RunEffectAppearanceTests( NAssetTest::FChecker& C );
	// Flipbookの保存値、コマ時計、timeline境界、JSON履歴を純粋計算で確認する。
	void RunEffectFlipbookTests( NAssetTest::FChecker& C );
	// Flipbook欄の実入力、選択、履歴と不正値の修復を確認する。
	void RunEffectFlipbookInputTests( NAssetTest::FChecker& C );
	// 保存定義を読み込んだ再生から粒子ごとのFlipbookコマを確認する。
	void RunEffectFlipbookPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 実simulationと描画を通し、Flipbookコマと描画結果を確認する。
	void RunEffectFlipbookRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// 偽backendでスプライトシートの派生画像、失敗時の解放、snapshot所有を確認する。
	void RunEffectFlipbookResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir );
	// 粒子の材質値、UV、マスク読み込みと従来命令との互換を再生から確認する。
	void RunEffectMaterialPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 材質付きRibbon/Lightningの点ごとの寿命値、UV、分割と旧命令互換を再生から確認する。
	void RunEffectMaterialPathPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir );
	// 材質付き四角形の実際の穴、境界発光、発光量、描画状態を確認する。
	void RunEffectMaterialRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor );
	// 材質付きRibbon/Lightningの実際の穴、境界発光、発光量、描画状態を確認する。
	void RunEffectMaterialRibbonRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor );
	// エフェクトのシーンでの確認（アセット・再生・部品・ワールドの Effect・描画システム。偽のバックエンド）。フレームをまたぐ 3 つの関数（WorldEffectSelfTestScene.cpp）:
	//   Start = 設置 -> Check = 1 フレーム以上あと。このフレームの描画コマンド Log を見る -> Finish = さらに 2 フレーム以上あと。片付ける
	void RunEffectSceneStart( NAssetTest::FChecker& C, ::CScene& Scene, ::CAssetManager& Assets, const ::FWorldSpawnerRegistry& Registry );
	void RunEffectSceneCheck( NAssetTest::FChecker& C, ::CScene& Scene, ::CAssetManager& Assets, const NAssetTest::FCommandLog& Log );
	void RunEffectSceneFinish( NAssetTest::FChecker& C, ::CScene& Scene, ::CAssetManager& Assets );
	// エフェクトの粒子を、本物の DxLib で描いたピクセルと突き合わせる（組み込みの丸・加算とアルファ・寿命の間の変化・回転・位置。WorldEffectSelfTestScene.cpp）。
	// Dir は、アセットのルート（quad.bmp = 4 x 4 の画像がある）
	void RunEffectRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir );
	// ワールドエディタの純粋な部分（光線と図形・カメラ・図形の作り方・ピッキング・ギズモ）の確認（WorldEditorSelfTestMath.cpp）
	void RunEditorMathTests( NAssetTest::FChecker& C );
	// ワールドエディタの編集の状態（履歴のまとめ・複数の編集・選択・セッション・アウトライナー・置き方）の確認（WorldEditorSelfTestSession.cpp）
	void RunEditorSessionTests( NAssetTest::FChecker& C );
	// ワールドエディタの視界への入力（クリック・矩形・ギズモのドラッグ・カメラの操作）の確認（WorldEditorSelfTestInput.cpp）
	void RunEditorInputTests( NAssetTest::FChecker& C );
	// ワールドエディタの計算を、本物の DxLib の絵と突き合わせる（カメラの投影・ビルボードとモデルの描いた範囲。WorldEditorSelfTestReal.cpp）。
	// Dir は、アセットのルート（モデルを書き出す）、QuadrantTexture は 4 x 4 の画像（quad.bmp）
	void RunEditorRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir, const TSharedPtr<CTexture>& QuadrantTexture );
}
