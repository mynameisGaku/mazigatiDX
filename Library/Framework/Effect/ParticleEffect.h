// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectAppearanceDefinition.h"
#include "Library/Framework/Effect/EffectForceFieldDefinition.h"
#include "Library/Framework/Effect/EffectFlipbookDefinition.h"
#include "Library/Framework/Effect/EffectMaterialDefinition.h"
#include "Library/Framework/Effect/EffectMeshDefinition.h"
#include "Library/Framework/Effect/EffectPathDefinition.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"
// 保存する.effect.jsonの書式番号。
constexpr int32 ParticleEffectFormatVersion = 1;

// 粒子を出す形（Direction の向きを軸にする）
enum class EParticleShape : uint8
{
	Cone,     // 底の円（ShapeRadius。0 なら 1 点）から、軸のまわりに ConeAngle（度）まで広がる向きへ
	Sphere,   // 球（ShapeRadius）の中から、中心の外向きへ
	Box,      // 箱（BoxSize）の中から、軸の向きへ
};

enum class EParticleBlend : uint8
{
	Alpha,    // 普通に重ねる（透明な煙・布）
	Add,      // 明るく足す（火・光・火花）
};

enum class EParticleSpace : uint8
{
	World,
	Local,
};

// 四角形の向き（描くときのカメラで決まる: FDrawContext::Quad3D の向きと同じ）
enum class EParticleOrient : uint8
{
	Camera,    // 画面に平ら
	CameraY,   // Y 軸だけ回って、カメラの方を向く
	Fixed,     // エフェクトの向き（X 軸・Y 軸の面）
};

const char* ToString( EParticleShape Shape );
const char* ToString( EParticleBlend Blend );
const char* ToString( EParticleSpace Space );
const char* ToString( EParticleOrient Orient );
// 大文字小文字は区別しない。知らない名前は false（Out は変えない）
bool TryParse( const FString& Text, EParticleShape& Out );
bool TryParse( const FString& Text, EParticleBlend& Out );
bool TryParse( const FString& Text, EParticleSpace& Out );
bool TryParse( const FString& Text, EParticleOrient& Out );

// 最小と最大（生まれるときに、この範囲のどれか）
struct FParticleRange
{
	float32 Min = 0.0f;
	float32 Max = 0.0f;
};

// 決まった時刻にまとめて出す
struct FParticleBurst
{
	float32 Time = 0.0f;     // エミッターが動き出してからの秒
	int32 Count = 10;
};

// 粒子の発生・運動・描画設定をまとめるエミッター定義。
struct FParticleEmitterDefinition
{
	FString Name = "Emitter";
	FString Texture;                                   // 粒子の画像。空なら、組み込みの、ふちがぼやけた丸
	EParticleBlend Blend = EParticleBlend::Add;
	EParticleSpace Space = EParticleSpace::World;
	EParticleOrient Orient = EParticleOrient::Camera;
	// 粒子を帯や分岐する雷として描く設定。None なら従来の四角形。
	FEffectPathDefinition Path;
	// 四角形・帯・雷の材質設定。
	FEffectMaterialDefinition Material;
	// 寿命に沿って大きさと RGBA を変える表示倍率。
	FEffectAppearanceDefinition Appearance;
	// 四角形粒子で共有するスプライトシートとコマ再生時計。
	FEffectFlipbookDefinition Flipbook;
	// 固定向きの平面または円環。Noneなら従来描画を保つ。
	FEffectMeshDefinition Mesh;

	int32 MaxParticles = 200;                          // 同時にいる粒子の上限（超える分は、出さない）
	float32 Delay = 0.0f;                              // エフェクトが始まってから、動き出すまでの秒
	float32 Rate = 20.0f;                              // 1 秒に出す数
	TArray<FParticleBurst> Bursts;                     // Time の小さい順に並べて保つ（FromJson / Sort が保つ）
	TArray<FEffectForceFieldDefinition> ForceFields;   // 粒子へ作用するWorld空間の力場。空なら従来の運動を保つ。

	FParticleRange Lifetime = { 0.5f, 1.0f };          // 秒（0 より大きい）
	FParticleRange Speed = { 1.0f, 2.0f };             // 生まれたときの速さ（m / 秒）

	EParticleShape Shape = EParticleShape::Cone;
	float32 ShapeRadius = 0.0f;                        // Cone の底・Sphere の半径（m）
	float32 ConeAngle = 15.0f;                         // Cone の広がり（度。0 でまっすぐ）
	FVector3 BoxSize = FVector3::One;                  // Box の大きさ（m）
	FVector3 Direction = FVector3::Up;                 // 軸の向き（エフェクトのローカル。長さは使わない）

	FVector3 Gravity = FVector3( 0.0f, -2.0f, 0.0f ); // 加速度（m / 秒^2。ワールドの向き）
	float32 Drag = 0.0f;                               // 空気の抵抗（1 秒に、速さが失われる割合。0 = なし）

	FParticleRange StartSize = { 0.2f, 0.3f };         // 四角形の 1 辺（m）。生まれるときに決まる
	FParticleRange EndSize = { 0.0f, 0.0f };           // 寿命が尽きるときの大きさ
	FColor32 StartColor = FColor32( 255, 255, 255, 255 );
	FColor32 EndColor = FColor32( 255, 255, 255, 0 );
	FParticleRange StartAngle = { 0.0f, 0.0f };        // 生まれたときの回転（度）
	FParticleRange Spin = { 0.0f, 0.0f };              // 回る速さ（度 / 秒）

	// Bursts を Time の小さい順に並べる
	void SortBursts();
};

// 検証の結果 1 つ
struct FParticleIssue
{
	bool bError = false;    // エラー = このままでは正しく動かない / 警告 = 動くが、意図と違うかもしれない
	int32 Emitter = -1;     // 問題のあるエミッター（エフェクト全体の問題なら -1）
	FString Message;
};

// 複数のエミッターと再生条件を持つ粒子エフェクト定義。
struct FParticleEffectDefinition
{
	static constexpr int32 MaxEmitters = 16;
	static constexpr int32 MaxParticlesPerEmitter = 20000;

	int32 Version = ParticleEffectFormatVersion;
	FString Name;
	float32 Duration = 0.0f;                           // エミッターが動く秒。0 = 止めるまで続く
	bool bLoop = true;                                 // Duration のあと、最初から繰り返すか（false なら、粒子が消えたら終わり）
	uint32 Seed = 1;                                   // 乱数のシード（再生のときに、別のシードを指定できる）
	TArray<FParticleEmitterDefinition> Emitters;

	NlohmannJson ToJson() const;
	// JSON から。オブジェクトでなければ false。足りない項目は既定値。直した内容・読めなかった項目は OutWarnings に書く（省略できる）
	static bool FromJson( const NlohmannJson& Json, FParticleEffectDefinition& OutDefinition, TArray<FString>* OutWarnings = nullptr );

	// 問題の一覧（エラーのまま動かしても、落ちない: 動かす側が、値を安全な範囲に丸める）
	TArray<FParticleIssue> Validate() const;
	static bool HasErrors( const TArray<FParticleIssue>& Issues );

	// 内容の識別（変わったかの判定・キャッシュの鍵）
	uint64 ComputeHash() const;
};
