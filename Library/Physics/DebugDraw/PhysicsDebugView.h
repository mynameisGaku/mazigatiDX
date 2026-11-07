// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Physics/IPhysicsDebug.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Physics/DebugDraw/PhysicsDebugDrawContext.h"

//=============================================================================
// NPhysicsDebugView
//   物理の世界の「形の線」（剛体・外接の箱・関節・キャラクター）を、ゲームの画面に重ねて出す、デバッグビュー。物理を使うシーン（CPhysicsSubsystem がある所）で、
//   オンにすれば、自動で描かれる（ゲームのコードは何もしなくてよい）。
//
//   オン / オフ:  F4（デバッグ画面を使っているとき）/ デバッグ画面のパネルの "Physics" / コンソール view Physics on（off / toggle）/ 起動引数 --physics-debug
//   調整:         デバッグ画面のパネルの Physics の項目 / コンソール view.set Physics static off、view.set Physics radius 30
//                 項目: static kinematic dynamic sensors（種類ごとに描くか）、bounds（外接の箱）、constraints（関節）、characters、
//                       region（範囲を絞る）+ radius + centerx / centery / centerz（その中心）、segments（円の線の数）、maxtriangles（形が細かいものの上限）
//   コードから:   NPhysicsDebugView::SetEnabled( true );  NPhysicsDebugView::GetSettings().bDrawStatic = false;
//
//   - 線は、既定では 3D の線（FDrawContext::Line3D。いまのカメラで見える）。2D のゲームなど、物理の座標が、画面にそのまま対応しないときは、SetLineMapper で、
//     線 1 本を、ゲームの画面の座標に変えて描く関数を渡す（シーンの OnEnter で渡し、OnExit で空にする。サンプルの CGameScene を参照）
//   - 設定は、プロセスで 1 つ（シーンが変わっても保たれる）。色や細かい項目は FPhysicsDebugDrawSettings（PhysicsDebugTypes.h）
//   - 描くのは、シーン全体の描画（CScene::AddSceneDraw）。物理の更新とは、同じスレッドで、重ならない
//   - 描いた線の本数は GetLastLineCount()（デバッグ画面に出る）
//=============================================================================
namespace NPhysicsDebugView
{
	// 名前（デバッグビューの登録口・コンソールで使う）
	constexpr const char* ViewName = "Physics";
	// シーン全体の描画の名前（CScene::AddSceneDraw）
	constexpr const char* SceneDrawName = "Physics.DebugDraw";

	// デバッグビューの登録口（FDebugViewRegistry）に、"Physics" を登録する。何度呼んでもよい。起動引数 --physics-debug があれば、最初の 1 回で、オンにする
	void Register();

	bool IsEnabled();
	void SetEnabled( bool bEnabled );

	// 描き方（種類ごとの有無・範囲・色など）。オンのとき毎フレーム使われる
	FPhysicsDebugDrawSettings& GetSettings();

	// オンのとき、Debug の世界を、Draw へ線として記録する。オフなら何もしない。描いた線の本数を返す（オフなら 0）
	int32 Draw( const IPhysicsDebug& Debug, FDrawContext& Draw );

	// 線を、Line3D の代わりに描く関数（空で、Line3D に戻す）。物理の座標の A・B を、ゲームの画面の座標に変えて、Draw に描く。
	// プロセスで 1 つ。ほかのシーンに残らないよう、使うシーンの OnExit で、空にすること
	using FLineMapper = FPhysicsDebugDrawContext::FLineMapper;
	void SetLineMapper( const FLineMapper& Mapper );

	// 直近の Draw で描いた線の本数
	int32 GetLastLineCount();
}
