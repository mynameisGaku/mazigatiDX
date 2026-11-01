// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector2.h"

//=============================================================================
// ピクセルアートのエフェクトの値の型（列挙・設定・データだけ。機能を持つクラスは、それぞれのファイル）
//   座標は、描き先（IPixelSurface）の画素の座標（左上が (0, 0)、右が +x、下が +y。1 単位 = 1 画素）。
//=============================================================================

// 暗い色から明るい色へ並んだ、既定の色の段（パレット）
enum class EPixelPalette : uint8
{
	Fire,
	Ember,
	Lava,
	Smoke,
	Dust,
	Ice,
	Water,
	Electric,
	Volt,
	Poison,
	Holy,
	Heal,
	Arcane,
	Shadow,
	Blood,
	Earth,
	Stone,
	Gold,
	Leaf,
	Wood,
	Metal,
	Bone,
	Neon,
	Cyber,
	Matrix,
	Candy,
	Night,
};

// 組み込みのドット文字
enum class EPixelFont : uint8
{
	Mini,   // 高さ 5 画素（小さな数値・ラベル）
	Big,    // 高さ 7 画素（見出し）
};

// ドット文字の横の寄せ方
enum class EPixelTextAlign : uint8
{
	Left,
	Center,
	Right,
};

// ドット文字の描き方
struct FPixelTextStyle
{
	EPixelFont Font = EPixelFont::Mini;
	// 拡大率（1 以上）
	int32 Scale = 1;
	EPixelTextAlign Alignment = EPixelTextAlign::Left;
	// 1 画素の縁取り
	bool bOutline = false;
	FColor32 OutlineColor = FColor32( 0, 0, 0, 255 );
};

// 図形の描き方（点・線・塗り・輪に共通）
struct FPixelStroke
{
	// 濃さ（0〜1）。1 未満は、4 x 4 のディザで画素を間引いて表す（ドット絵らしく、色は混ぜない）
	float32 Alpha = 1.0f;
	// 線と輪の太さ（画素。1 以上）
	int32 Width = 1;
	// 同じ画素に足す光の強さ（0 なら光らない）
	float32 Glow = 0.0f;
};

// エフェクトを描く層。キャラクターの後ろ（煙・背面のオーラ）と前（火花・炎）
enum class EPixelLayer : uint8
{
	Back,
	Front,
};

// プレビューや見本で、エフェクトの後ろに敷く背景
enum class EPixelBackdrop : uint8
{
	Dark,     // 暗い単色
	Ground,   // 暗い空 + 地面から下の石畳
	Sky,      // 星空のグラデーション（ディザの段）
	Light,    // 明るい単色
	Black,    // 真っ黒
};

// 2 値のシルエット（Width x Height。0 = 空・1 = 塗り）
struct FPixelMask
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint8> Data;

	FORCEINLINE bool IsValid() const
	{
		return Width > 0 && Height > 0 && Data.Num() == Width * Height;
	}

	// (X, Y) が塗りか（範囲の外は false）
	FORCEINLINE bool IsSet( int32 X, int32 Y ) const
	{
		return IsValid() && X >= 0 && Y >= 0 && X < Width && Y < Height && Data[Y * Width + X] != 0;
	}
};

// 状態変化・オーラ・被弾の演出を試すための、立ち絵のキャラクター（足元の中心が (0, 0)。絵は (-Width / 2, -Height) から）
struct FPixelActor
{
	FPixelMask Mask;
	// Width x Height の色（a == 0 は空）
	TArray<FColor32> Pixels;
	int32 Width = 0;
	int32 Height = 0;
	// 頭のてっぺんと胸の位置（足元からの相対）
	FVector2 Head;
	FVector2 Chest;
};

// 粒子の形
enum class EPixelParticleShape : uint8
{
	Dot,     // 1 画素
	Disc,    // 半径 Size の塗りの円（煙・泡）
	Streak,  // 速度の向きに伸びた線（火花。速いほど長い）
	Cross,   // 十字（きらめき。腕の長さは Size）
};

// 粒子 1 つ（位置は、エフェクトの基準点からの相対の画素）
struct FPixelParticle
{
	FVector2 Position;
	// 画素 / 秒
	FVector2 Velocity;
	float32 Age = 0.0f;
	// 寿命（秒）。Age がこれを超えたら消える
	float32 Life = 1.0f;
	// 下向きの加速度（画素 / 秒^2。負なら上へ昇る）
	float32 Gravity = 0.0f;
	// 1 秒あたりの速度の減り方（0 = 減らない。2 なら 1 秒でおよそ 1/7 に）
	float32 Drag = 0.0f;
	float32 Size = 1.0f;
	// 生まれたときの色の段の位置（0 = 最も暗い・1 = 最も明るい）。寿命に合わせて 0 へ冷える
	float32 Heat = 1.0f;
	// 同じ画素に足す光の強さ（冷えるにつれて弱まる）
	float32 Glow = 0.0f;
	// 寿命の終わりに、ディザで薄れて消える
	bool bFade = true;
	EPixelParticleShape Shape = EPixelParticleShape::Dot;
	EPixelLayer Layer = EPixelLayer::Front;
};

// エフェクトを置く場所と、その再生の選び方
struct FPixelPlacement
{
	// 描き先の画素の座標で、エフェクトの基準点（足元・当たった点・発射口など）を置く所
	FVector2 Position;
	// 左右を反転する（向きのある演出）
	bool bFlipHorizontal = false;
	// 乱数の種（同じ種なら、毎回同じ絵になる）
	uint32 Seed = 1;
	// 色の段（空なら、エフェクトの既定のパレット）。暗い色から明るい色へ
	TArray<FColor32> Palette;
};

// エフェクト 1 種の情報
struct FPixelEffectMeta
{
	FString Name;
	FString Category;
	// 見本で描くときの描き先の大きさと、基準点を置く場所
	int32 Width = 96;
	int32 Height = 96;
	FVector2 Anchor = FVector2( 48.0f, 48.0f );
	// 一度きりの長さ（秒）。bLoop なら、この長さで繰り返す
	float32 Duration = 1.0f;
	bool bLoop = false;
	EPixelPalette DefaultPalette = EPixelPalette::Fire;
};
