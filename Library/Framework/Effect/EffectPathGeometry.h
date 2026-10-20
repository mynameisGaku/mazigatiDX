// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_EFFECTPATHGEOMETRY_H
#define FRAMEWORK_EFFECT_EFFECTPATHGEOMETRY_H

#include "Library/Framework/Effect/EffectPathDefinition.h"
#include "Library/Math/Color.h"

// 描画方式に依存しない、ワールド座標の帯の中心線と各点の見た目。
struct FEffectPathGeometry
{
	// 帯の中心位置・全幅・色・累積距離を持つ一点。
	struct FPoint
	{
		// 確定したワールド座標。
		FVector3 Position = FVector3::Zero;
		// 帯の全幅。枝の先端では 0。
		float32 Width = 0.0f;
		// 粒子の寿命による色の変化を反映した色。
		FColor32 Color;
		// 帯の始点からの累積ワールド距離。画像の繰り返しは描画時に換算する。
		float32 U = 0.0f;
		// この点を作った粒子の経過秒。UV の移動はこの値を使う。
		float32 AgeSeconds = 0.0f;
		// この点を作った粒子の寿命に対する経過割合。材質曲線を評価する。
		float32 NormalizedAge = 0.0f;
	};
	// 途中で切れずにつながる、順番を持つ一点の列。
	struct FStrip
	{
		// 始点から終点へ並んだ点。
		TArray<FPoint> Points;
	};
	// 主幹と枝を含めても超えない、一回の生成の点数。
	static constexpr int32 MaxPoints = ( FEffectPathDefinition::MaxSegments + 1 ) * ( FEffectPathDefinition::MaxBranches + 1 );
	// 最初が主幹、その後が独立した枝の中心線。
	TArray<FStrip> Strips;
};

namespace NEffectPath
{
	// 確定したワールドの両端と全幅から、時刻とシードで決まる雷の中心線を作る。
	// 主幹の両端は厳密に一致し、枝は主幹の途中から始まる。形の更新間は滑らかに補間する。
	// Out は最初に空にする。雷以外・非有限の両端・同一点・非正の幅では空のまま。
	void BuildLightning( const FEffectPathDefinition& Definition, uint32 Seed, float32 Time, const FVector3& Start, const FVector3& End, float32 Width, const FColor32& Color, FEffectPathGeometry& Out );
	// 完成形をシードで固定して作り、経過秒に応じた成長または放電だけを出力する。
	// TreeLightning以外・非有限入力・同一点・非正の幅・不正なTree設定では空のまま。
	void BuildTreeLightning( const FEffectPathDefinition& Definition, uint32 Seed, float32 Age, const FVector3& Start, const FVector3& End, float32 Width, const FColor32& Color, FEffectPathGeometry& Out );
	// 全seedの幹・一次枝・二次枝を含む保守的な球の中心と半径を計算する。
	// TreeLightning以外、非有限座標、同一点、不正なJitter/枝長、負または非有限paddingではfalseを返す。
	bool GetTreeLightningBounds( const FEffectPathDefinition& Definition, const FVector3& Start, const FVector3& End, float32 Padding, FVector3& OutCenter, float32& OutRadius );
}

#endif
