// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Source/Common/PlatformerStatus.h"

class CBlackboard;

//=============================================================================
// IPlatformerPlayerBody / IPlatformerEnemyBody
//   2D と 3D の、主人公と敵の「体」の窓口。ステートマシン（Samples/Assets/Platformer/*.gxstate）の、ステートの処理
//   （Common/PlatformerParts.cpp に登録した部品）は、この窓口の関数だけを呼ぶ。
//   どのステートにいるか、いつ遷移するかは、ステートマシンのデータが決める。体（Platformer2D / 3D のクラス）は、
//   ステートごとの「動き」だけを持つ（次のフレームへ進める・始まるときの準備）。コードに、状態の enum と switch はない。
//=============================================================================
class IPlatformerPlayerBody
{
public:
	virtual ~IPlatformerPlayerBody() = default;

	// 姿が決まった（Small / Big / Fire の各ステートに入るとき）
	virtual void OnFormEntered( EPlatformerForm Form ) = 0;

	// 敵に当たって小さくなった直後の、ひと息（無敵の時間）を始める
	virtual void BeginHurtBreather() = 0;

	// 生きている間の、毎フレームの動き（操作・移動・当たり・拾う・踏む・撃つ）。やられた・ゴールは、イベントで知らせる
	virtual void StepAlive( float32 DeltaTime ) = 0;

	// 生きている状態に入った（やられたあと、続きから始めるなら、最初の位置へ戻る）
	virtual void EnterAlive() = 0;

	// やられた: 始まるとき（残機を 1 つ減らして、跳ね上がる）と、落ちていく間の動き
	virtual void BeginDeath() = 0;
	virtual void StepDying( float32 DeltaTime ) = 0;

	// ゴール: 始まるとき（点数・印）と、ゴール後の動き（ポールをすべり降りる / くるくる回る）
	virtual void BeginGoal() = 0;
	virtual void StepGoal( float32 DeltaTime ) = 0;

	// ゲームオーバーになった
	virtual void BeginGameOver() = 0;
};

//=============================================================================
// IPlatformerPilotBody
//   自動操作（ビヘイビアツリー Autopilot*.gxbt）が見る、主人公のまわりの様子。主人公が実装する（センサー）。
//   Sense が、跳ぶべきか（ShouldJump）・火の玉を撃てるか（CanShoot）・近くにアイテムがあるか（ItemNear / ItemMoveX / ItemMoveY）を、
//   ブラックボードへ書く。ツリーの Task は、操作（MoveX / MoveY / Run / JumpPressed / JumpHeld / FirePressed）を、ブラックボードへ書く。
//=============================================================================
class IPlatformerPilotBody
{
public:
	virtual ~IPlatformerPilotBody() = default;

	virtual void Sense( CBlackboard& Blackboard ) = 0;
};

class IPlatformerEnemyBody
{
public:
	virtual ~IPlatformerEnemyBody() = default;

	// 向いている向きに、速さ Speed で歩く（重力つき。壁で向きを変える。bAvoidEdges なら、崖の前で向きも変える）
	virtual void StepWalk( float32 DeltaTime, float32 Speed, bool bAvoidEdges ) = 0;

	// 主人公へ向かって、速さ Speed で歩く（3D の追いかける敵）
	virtual void StepChase( float32 DeltaTime, float32 Speed ) {}

	// 止まったまま（重力だけ）
	virtual void StepRest( float32 DeltaTime ) = 0;

	// すべる甲羅の動き（Speed で進み、ぶつかった敵を倒す）と、始まるとき（蹴られた向きを決める）
	virtual void StepSlide( float32 DeltaTime, float32 Speed ) {}
	virtual void BeginSlide() {}

	// 甲羅にこもったとき（背が低くなって、止まる）
	virtual void BeginShell() {}

	// つぶされたとき（ぺしゃんこの姿になる）
	virtual void BeginSquash() = 0;

	// 吹き飛ばされた間の動き（飛んで、落ちていく）と、始まるとき
	virtual void BeginFly() = 0;
	virtual void StepFly( float32 DeltaTime ) = 0;

	// 消える
	virtual void Remove() = 0;
};
