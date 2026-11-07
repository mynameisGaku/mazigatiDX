// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// プロファイラーの「分類」（Category）の、正式な一覧
//   プロファイラーは、計測を 分類（何の処理か）・持ち主（どの持ち主の下か）・名前（処理や型）・個体 の 4 つの軸で分ける（ProfilerTypes.h）。
//   このうち、分類は、画面（Category の列・Group by Category）の並び・色・絞り込みの単位になる。文字列を、あちこちで自由に書くと、
//   綴りの違い（"Physic" と "Physics"）で、別の分類に割れてしまうので、ライブラリの中は、ここの定数を使う。
//
//   PROFILE_SCOPE_CAT( NProfileCategory::Physics, "Jolt.Update" );
//   PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "ScreenFlip" );
//   PROFILE_COUNTER( "Asset.Count", Count );        // カウンターの名前は「<分類>.<名前>」。最初の '.' の前が、下の一覧のどれか
//
//   決まり:
//     - Library の中では、PROFILE_SCOPE_CAT / PROFILE_SCOPE_OWNER / FProfileScope の分類に、文字列リテラルを書かない（定数を使う）。
//       カウンター名の最初の '.' の前も、この一覧のどれか（CheckSourceFormat が検査する）
//     - 新しい分類が要るときは、ここに足す（コメントに、何の処理かを書く）。ゲーム側（Source / Samples）は、自分の分類を、文字列で決めてよい
//     - 持ち主（Owner）と名前（Name）は、分類の下で、自由に決めてよい（シーン名・型名・"Platform.EndFrame" など）
//=============================================================================
namespace NProfileCategory
{
	//-------------------------------------------------------------------------
	// フレームの流れ
	//-------------------------------------------------------------------------
	// エンジンの 1 フレームの、大きな区切り（Platform.BeginFrame・GameInstance.Tick・GameThreadTasks など）
	constexpr const char* Engine = "Engine";
	// OS・描画・ウィンドウの実装（DxLib・ImGui・Null）の呼び出し（ScreenFlip・ProcessMessage・FrameLimiter など）
	constexpr const char* Platform = "Platform";
	// ゲームインスタンスの Tick / Draw と、そのサブシステム
	constexpr const char* GameInstance = "GameInstance";
	// シーンの Tick / Draw の区切り（持ち主 = シーン名。Transforms.Update・Objects.Draw・SceneDraws など）
	constexpr const char* Scene = "Scene";

	//-------------------------------------------------------------------------
	// 仕組みごとの処理
	//-------------------------------------------------------------------------
	// サブシステムの Tick（名前 = 型名）
	constexpr const char* Subsystem = "Subsystem";
	// ゲームオブジェクトのコンポーネントの Tick / Draw
	constexpr const char* Component = "Component";
	// ECS のシステムの実行
	constexpr const char* System = "System";
	// ジョブシステム（ワーカースレッドで動く仕事）
	constexpr const char* Job = "Job";
	// 物理（Jolt の更新・接触の通知・関節・キャラクター）
	constexpr const char* Physics = "Physics";
	// 音声（更新・再生）
	constexpr const char* Audio = "Audio";
	// アセット（読み込み・Tick・変換）
	constexpr const char* Asset = "Asset";
	// AI（ステートマシン・ビヘイビアツリー）
	constexpr const char* AI = "AI";
	// デバッグ表示そのもの（デバッグウィンドウ・コンソール・プロファイラーのウィンドウなど。計測の邪魔をしているかを見る）
	constexpr const char* Debug = "Debug";

	//-------------------------------------------------------------------------
	// マクロの既定
	//-------------------------------------------------------------------------
	// PROFILE_SCOPE( Name ) の分類（分類を決めずに、名前だけで測るとき）
	constexpr const char* General = "General";
	// PROFILE_FUNCTION() の分類
	constexpr const char* Function = "Function";

	//-------------------------------------------------------------------------
	// カウンターだけの分組（PROFILE_COUNTER の名前の頭。時間の計測には使わない）
	//-------------------------------------------------------------------------
	// メモリの使用量（作業セット・ビデオメモリ）
	constexpr const char* Memory = "Memory";
	// バックエンドが持つハンドルの数（テクスチャ・モデル・音・フォント）
	constexpr const char* Handle = "Handle";
	// プールの使用率
	constexpr const char* Pool = "Pool";
}
