// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Math/Math.h"

//=============================================================================
// 3D の描画状態の値（深度バッファ・フォグ・画像の貼り方の変換）
//   純粋な値と計算（DxLib を使わない）。描画に使うときは、FDrawContext の Depth / Fog / HeightFog / TextureTransform などで記録する
//   （実行のときに DxLib へ反映する）。どれも、そのオブジェクトの分（1 回の Draw）が終わると、始める前の状態に戻る。
//
//   CommandList.Depth( FDepthState::ReadOnly() );                                              // 半透明の物: 手前の物には隠れるが、あとの物を隠さない
//   CommandList.Fog( FFogSettings::MakeLinear( FColor32( 120, 150, 190 ), 20.0f, 80.0f ) );    // 奥行き 20 から 80 にかけて、空の色にかすむ
//   CommandList.TextureTransform( FTextureTransform::MakeScroll( FVector2( Time * 0.1f, 0.0f ) ) );   // 水面の画像を流す
//=============================================================================

//=============================================================================
// FDepthState
//   深度バッファ（Z バッファ）の使い方。深度バッファは、画素ごとに「いちばん手前の物の奥行き」を覚えていて、
//   あとから描く物が、それより奥なら描かない（手前の物が奥の物を隠す）仕組み。
//=============================================================================
struct FDepthState
{
	// Bias の範囲（これより大きい値は、この範囲に収める）
	static constexpr int32 MaxBias = 100000;

	// 描くとき、深度バッファの値と比べるか（false なら、比べずに、描いた順に上書きする）
	bool bTestDepth = true;

	// 描いた物の奥行きを、深度バッファに書くか（false なら、描いた物は、あとから描く物の邪魔をしない。半透明の物に）
	bool bWriteDepth = true;

	// 比べ方（描く物の奥行き と 深度バッファの値）。既定の LessEqual は、同じか手前なら描く
	EDrawCompare Compare = EDrawCompare::LessEqual;

	// 比べる奥行きに足す偏り。負の値は手前に寄る（同じ平面に重ねた物のちらつきを避けるのに使う）。DxLib の SetZBias3D と同じ
	int32 Bias = 0;

	// ふつう（比べて、書く）
	static FDepthState Normal()
	{
		return FDepthState();
	}

	// 比べるだけで、書かない。半透明の物に: 手前の物には隠れるが、描いた物があとの物を隠さない
	static FDepthState ReadOnly()
	{
		FDepthState State;
		State.bWriteDepth = false;
		return State;
	}

	// 比べず、書かない。描いた順に重なる。空や画面の飾りに
	static FDepthState Disabled()
	{
		FDepthState State;
		State.bTestDepth = false;
		State.bWriteDepth = false;
		return State;
	}

	// 値が使えるか（Compare が EDrawCompare の範囲の中）
	bool IsValid() const
	{
		return static_cast<int32>( Compare ) >= static_cast<int32>( EDrawCompare::Never ) && static_cast<int32>( Compare ) <= static_cast<int32>( EDrawCompare::Always );
	}
};

//=============================================================================
// FFogSettings
//   フォグ（遠くの物を、フォグの色にかすませる）。距離によるフォグ（FDrawContext::Fog）と、高さによるフォグ（HeightFog）に使う。
//   球・箱・カプセル・ポリゴン・画像を貼った四角形・モデルに掛かる。三角形（Triangle3D）・線（Line3D）・ビルボード（Billboard3D）には、DxLib の仕様で掛からない。
//
//   Fog（距離）  Linear: 奥行き Start でフォグが 0、End で 1 になる。Exponential: 1 / exp( 距離 x Density )。ExponentialSquared: 1 / exp( ( 距離 x Density )^2 )
//                距離は、視点からの奥行き（ワールドの単位）。物の面の奥行きで決まる。
//   HeightFog    Linear: 高さ Start でフォグが 0、End で 1 になる（Start より End が低くてもよい）。
//                Exponential / ExponentialSquared: 高さ Start より下で、低いほど濃くなる（Start より上は、フォグなし）。Density は、下へ離れる距離あたりの濃さ。
//=============================================================================
struct FFogSettings
{
	// 濃さの決め方。None ならフォグなし（Fog / HeightFog に渡すと、フォグをやめる）
	EDrawFogMode Mode = EDrawFogMode::None;

	// フォグの色（アルファは使わない）
	FColor32 Color = FColor32( 128, 128, 128 );

	// Linear の始まりと終わり（Fog なら奥行き、HeightFog なら高さ）。HeightFog の Exponential / ExponentialSquared では、Start が「ここより下に掛かる」高さ
	float32 Start = 0.0f;
	float32 End = 1000.0f;

	// Exponential / ExponentialSquared の濃さ（0 以上。大きいほど、早く濃くなる）
	float32 Density = 0.01f;

	// フォグなし
	static FFogSettings Off()
	{
		return FFogSettings();
	}

	// 直線のフォグ: Start で 0、End で 1
	static FFogSettings MakeLinear( const FColor32& InColor, float32 InStart, float32 InEnd )
	{
		FFogSettings Settings;
		Settings.Mode = EDrawFogMode::Linear;
		Settings.Color = InColor;
		Settings.Start = InStart;
		Settings.End = InEnd;
		return Settings;
	}

	// 指数のフォグ。InStart は、HeightFog で「ここより下に掛かる」高さ（Fog では使わない）
	static FFogSettings MakeExponential( const FColor32& InColor, float32 InDensity, float32 InStart = 0.0f )
	{
		FFogSettings Settings;
		Settings.Mode = EDrawFogMode::Exponential;
		Settings.Color = InColor;
		Settings.Density = InDensity;
		Settings.Start = InStart;
		return Settings;
	}

	// 指数の 2 乗のフォグ（近くは薄く、あるところから急に濃くなる）。InStart は、HeightFog で「ここより下に掛かる」高さ（Fog では使わない）
	static FFogSettings MakeExponentialSquared( const FColor32& InColor, float32 InDensity, float32 InStart = 0.0f )
	{
		FFogSettings Settings = MakeExponential( InColor, InDensity, InStart );
		Settings.Mode = EDrawFogMode::ExponentialSquared;
		return Settings;
	}

	// 値が使えるか（Mode が範囲の中。Start / End / Density が有限。Density が 0 以上。Linear では Start と End が違う）
	bool IsValid() const
	{
		const int32 ModeValue = static_cast<int32>( Mode );
		if ( ModeValue < static_cast<int32>( EDrawFogMode::None ) || ModeValue > static_cast<int32>( EDrawFogMode::Linear ) )
		{
			return false;
		}
		if ( !NMath::IsFinite( Start ) || !NMath::IsFinite( End ) || !NMath::IsFinite( Density ) || Density < 0.0f )
		{
			return false;
		}
		return Mode != EDrawFogMode::Linear || Start != End;
	}
};

//=============================================================================
// FTextureTransform
//   3D の面に貼る画像の、貼る位置・拡大・回転（画像の座標 UV を動かす）。水面や雲を流したり、模様を繰り返したりするのに使う。
//   画像の外側の貼り方は、FDrawContext::TextureAddress（既定は Wrap = 繰り返す）。
//   面の UV を、次の順に動かした座標で、画像を引く（DxLib の順番。実測）:
//     1. RotationCenter を中心に、Rotation だけ回す  2. Translation を足す  3. Scale を掛ける（原点から）
//     Scale が 2 なら、同じ画像が 2 倍の回数、面に繰り返し貼られる。Translation の x が正なら、画像は左へ流れて見える。
//     Rotation が正なら、画像は、RotationCenter を中心に、画面で反時計回りに回って見える。
//     Scale は最後に掛かるので、Scale と Translation を一緒に使うと、ずれも拡大される（Translation x 0.5 を Scale 2 で、UV 1 だけ動く）。
//=============================================================================
struct FTextureTransform
{
	// UV に足す量（UV の単位。(0.5, 0) で、横へ半分ずれる）
	FVector2 Translation = FVector2( 0.0f, 0.0f );

	// UV に掛ける倍率
	FVector2 Scale = FVector2( 1.0f, 1.0f );

	// 回転の中心（UV）
	FVector2 RotationCenter = FVector2( 0.5f, 0.5f );

	// 回転（ラジアン）
	float32 Rotation = 0.0f;

	// 画像を流す（Translation だけ）
	static FTextureTransform MakeScroll( const FVector2& InTranslation )
	{
		FTextureTransform Transform;
		Transform.Translation = InTranslation;
		return Transform;
	}

	// 画像を繰り返す回数を変える（Scale だけ）
	static FTextureTransform MakeScale( const FVector2& InScale )
	{
		FTextureTransform Transform;
		Transform.Scale = InScale;
		return Transform;
	}

	// 画像を回す（Rotation と、その中心）
	static FTextureTransform MakeRotation( float32 InRadians, const FVector2& InCenter = FVector2( 0.5f, 0.5f ) )
	{
		FTextureTransform Transform;
		Transform.Rotation = InRadians;
		Transform.RotationCenter = InCenter;
		return Transform;
	}

	// 何も変えない変換か（Translation が 0、Scale が 1、Rotation が 0。回転の中心は関係ない）
	bool IsIdentity() const
	{
		return Translation.x == 0.0f && Translation.y == 0.0f && Scale.x == 1.0f && Scale.y == 1.0f && Rotation == 0.0f;
	}

	// 値が使えるか（すべて有限）
	bool IsValid() const
	{
		return NMath::IsFinite( Translation.x ) && NMath::IsFinite( Translation.y ) && NMath::IsFinite( Scale.x ) && NMath::IsFinite( Scale.y )
			&& NMath::IsFinite( RotationCenter.x ) && NMath::IsFinite( RotationCenter.y ) && NMath::IsFinite( Rotation );
	}

	// 面の UV が、この変換で、画像のどこを引くことになるか（回す -> ずらす -> 拡大の順）
	FVector2 Apply( const FVector2& UV ) const
	{
		const float32 Cos = NMath::Cos( Rotation );
		const float32 Sin = NMath::Sin( Rotation );
		const float32 DeltaX = UV.x - RotationCenter.x;
		const float32 DeltaY = UV.y - RotationCenter.y;
		const float32 RotatedX = RotationCenter.x + DeltaX * Cos - DeltaY * Sin + Translation.x;
		const float32 RotatedY = RotationCenter.y + DeltaX * Sin + DeltaY * Cos + Translation.y;
		return FVector2( RotatedX * Scale.x, RotatedY * Scale.y );
	}
};
