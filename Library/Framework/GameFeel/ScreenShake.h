// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector2.h"

//=============================================================================
// FScreenShake: 画面の揺れ（大きな衝撃を、画面ごと揺らして伝える）
//   揺れのずれ（横・縦）を計算するだけ。ずれを、2D なら描く位置やスクロールに、3D ならカメラの位置と注視点に足して使う。
//   最初が強く、だんだん収まる。大・中・小は、大きさだけでなく、揺れ方が違う（小は縦だけ、中と大は横も）。
//
//   FScreenShake Shake;
//   Shake.Start( EScreenShakeStrength::Medium );     // 当たった・着地した・爆発した
//   Shake.Advance( DeltaTime );                      // 毎フレーム（実時間。ヒットストップの間も、進める）
//   const FVector2 Offset = Shake.GetOffset();       // 画素（1280 x 720 の画面で、ちょうどよい大きさ）
//   Camera.Position += Right * ( Offset.x * 0.01f ) + Up * ( Offset.y * 0.01f );   // 3D: ワールドの長さに直して、カメラへ
//
//   - 揺れている間に、また Start を呼ぶと、いまの揺れの大きさと比べて、大きいほうにする（小さい揺れで、大きい揺れが消えない）。
//   - Scale で、大きさを変えられる（カメラが遠いときは大きく、近いときは小さく。揺れを弱める設定にも使える。0 で揺れない）。
//   - やりすぎると、画面が見にくくなる。使うのは、はっきりした衝撃だけにする。
//=============================================================================
enum class EScreenShakeStrength : uint8
{
	Small,    // 縦だけの、短い揺れ（軽い攻撃が当たった・着地）
	Medium,   // 横も入る揺れ（強い攻撃・ダメージを受けた）
	Large     // 大きく、長い揺れ（爆発・ボスを倒した・やられた）
};

// 揺れ方（大きさは画素。1 秒に Frequency 回、往復する）
struct FScreenShakeSettings
{
	float32 Duration = 0.2f;
	float32 AmplitudeX = 0.0f;
	float32 AmplitudeY = 6.0f;
	float32 Frequency = 30.0f;

	// 大・中・小の、決まった揺れ方
	static FScreenShakeSettings Make( EScreenShakeStrength Strength );
};

class FScreenShake
{
public:
	void Start( EScreenShakeStrength Strength, float32 Scale = 1.0f );
	// 揺れ方を、自分で決める。長さ・大きさが 0 以下・有限でない数なら、何もしない
	void Start( const FScreenShakeSettings& Settings, float32 Scale = 1.0f );
	void Stop();

	// 実時間を DeltaTime 秒進める（負・有限でない数は、進めない）
	void Advance( float32 DeltaTime );

	bool IsActive() const { return Elapsed < Settings.Duration; }
	// いまのずれ（画素。揺れていなければ 0）
	FVector2 GetOffset() const;
	// いまの揺れの大きさ（画素。収まっていく包みの大きさ。揺れていなければ 0）
	float32 GetCurrentAmplitude() const;

private:
	FScreenShakeSettings Settings = FScreenShakeSettings { 0.0f, 0.0f, 0.0f, 30.0f };
	float32 Elapsed = 0.0f;
	// 揺れごとに、形を少し変える数（始めるたびに進む）
	uint32 Variation = 0;
};
