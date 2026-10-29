// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/ImageTypes.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Color.h"
#include "Library/Math/Math.h"

//=============================================================================
// FGraphFilter / FGraphBlend
//   画像にフィルターを掛ける・2 枚の画像を混ぜるときの、設定の値（DxLib の GraphFilter / GraphBlend に当たる）。
//   FDrawContext::ApplyFilter / BlendImages に渡す。DxLib は呼ばない（実行のときに、実行先が行う）。
//
//   使い方:
//       CommandList.ApplyFilter( Source, Dest, FGraphFilter::Gauss( 16, 300 ) );     // Source をぼかして Dest へ（Dest は描画先）
//       CommandList.ApplyFilter( Target, FGraphFilter::Invert() );                  // Target の中身を、その場で反転
//
//   引数は、作るときに、使える範囲に収める（DxLib は、範囲の外でも失敗を返さず、おかしな結果にするので。例: Level のガンマが 0 以下）。
//   画素の色は、乗算されていないアルファ（ストレート）の画像として扱う。乗算済みアルファの画像には、bPremultiplied を true にする
//   （対応する種類だけ。DX_GRAPH_FILTER_PMA_*）。画像の中の、透明な画素の色は、フィルターによって変わることがある。
//=============================================================================
struct FGraphFilter
{
	EDrawGraphFilter Type = EDrawGraphFilter::Invert;
	// 乗算済みアルファの画像用の版を使うか（BrightClip / BrightScale / Hsb / Invert / Level / TwoColor / GradientMap / Replacement だけ）
	bool bPremultiplied = false;
	// 種類ごとの引数（下の関数を使えば、直接は触らない）
	int32 Params[8] = {};
	// GradientMap のグラデーションマップの画像
	FImageHandle Map;

	// 使える値か（種類が範囲の中で、GradientMap は、マップの画像が必要）
	FORCEINLINE bool IsValid() const
	{
		const int32 Value = static_cast<int32>( Type );
		return Value >= 0 && Value <= static_cast<int32>( EDrawGraphFilter::FloatColorScale ) && ( Type != EDrawGraphFilter::GradientMap || Map.IsValid() );
	}

	// 単色化（モノトーン）。青色差・赤色差（-255..255）で、色合いをずらせる（0, 0 は、灰色のまま）
	static FGraphFilter Mono( int32 BlueDifference = 0, int32 RedDifference = 0 )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Mono );
		Filter.Params[0] = NMath::Clamp( BlueDifference, -255, 255 );
		Filter.Params[1] = NMath::Clamp( RedDifference, -255, 255 );
		return Filter;
	}

	// ぼかし。PixelWidth は使うピクセルの幅（8・16・32 のどれか。それ以外は、近い大きいほうに）、Strength はぼかしの強さ（100 で約 1 ピクセル分。0 以下は、何も変わらない。上限 10000）
	static FGraphFilter Gauss( int32 PixelWidth, int32 Strength )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Gauss );
		Filter.Params[0] = PixelWidth <= 8 ? 8 : ( PixelWidth <= 16 ? 16 : 32 );
		Filter.Params[1] = NMath::Clamp( Strength, 0, 10000 );
		return Filter;
	}

	// 縮小。Divisor は元の何分の 1 か（1・2・4・8 のどれか。それ以外は、小さいほうに）。変換先（FilterBlt の Dest）は、元の 1 / Divisor の大きさにする
	static FGraphFilter DownScale( int32 Divisor )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::DownScale );
		Filter.Params[0] = Divisor >= 8 ? 8 : ( Divisor >= 4 ? 4 : ( Divisor >= 2 ? 2 : 1 ) );
		return Filter;
	}

	// 明るさ（0..255）が Threshold より暗い（bClipDarker）・明るい画素を、クリップする。bFill なら、FillColor（アルファも）で塗る。
	// 塗らない（bFill が false）なら、その画素は、書き込まれず、変換先のそのまま（その場なら、元のまま）
	static FGraphFilter BrightClip( bool bClipDarker, int32 Threshold, bool bFill, const FColor32& FillColor = FColor32( 0, 0, 0, 0 ), bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::BrightClip, bPremultiplied );
		Filter.Params[0] = bClipDarker ? 1 : 0;
		Filter.Params[1] = NMath::Clamp( Threshold, 0, 255 );
		Filter.Params[2] = bFill ? 1 : 0;
		Filter.Params[3] = PackRgb( FillColor );
		Filter.Params[4] = FillColor.a;
		return Filter;
	}

	// 明るさ MinBright（0..255）以下を真っ暗、MaxBright 以上を真っ白にして、間を広げる（MinBright が MaxBright より大きいと、明るさが反転する）
	static FGraphFilter BrightScale( int32 MinBright, int32 MaxBright, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::BrightScale, bPremultiplied );
		Filter.Params[0] = NMath::Clamp( MinBright, 0, 255 );
		Filter.Params[1] = NMath::Clamp( MaxBright, 0, 255 );
		return Filter;
	}

	// 色相・彩度・明るさ。色相は、いまの色相からの差（度。-180..180 に収める。360 で一周）。彩度は -255.. （0 は変えない、-255 で灰色）、明るさは -255..255（足す）
	static FGraphFilter HsbRelative( int32 Hue, int32 Saturation, int32 Bright, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Hsb, bPremultiplied );
		Filter.Params[0] = 0;
		Filter.Params[1] = WrapDegrees( Hue + 180 ) - 180;
		Filter.Params[2] = NMath::Clamp( Saturation, -255, 1000 );
		Filter.Params[3] = NMath::Clamp( Bright, -255, 255 );
		return Filter;
	}

	// 色相を、決まった値（度。0..359 に収める）にそろえる。彩度・明るさは HsbRelative と同じ
	static FGraphFilter HsbAbsolute( int32 Hue, int32 Saturation, int32 Bright, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Hsb, bPremultiplied );
		Filter.Params[0] = 1;
		Filter.Params[1] = WrapDegrees( Hue );
		Filter.Params[2] = NMath::Clamp( Saturation, -255, 1000 );
		Filter.Params[3] = NMath::Clamp( Bright, -255, 255 );
		return Filter;
	}

	// 階調（色）の反転。アルファは変わらない
	static FGraphFilter Invert( bool bPremultiplied = false )
	{
		return Make( EDrawGraphFilter::Invert, bPremultiplied );
	}

	// レベル補正。Min..Max（0..255）を、AfterMin..AfterMax（0..255）へ伸ばす。Gamma は 100 で補正なし（1..10000。1 未満は 1 に収める。DxLib は 0 以下で真っ暗になる）
	static FGraphFilter Level( int32 Min, int32 Max, int32 Gamma, int32 AfterMin, int32 AfterMax, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Level, bPremultiplied );
		Filter.Params[0] = NMath::Clamp( Min, 0, 255 );
		Filter.Params[1] = NMath::Clamp( Max, 0, 255 );
		Filter.Params[2] = NMath::Clamp( Gamma, 1, 10000 );
		Filter.Params[3] = NMath::Clamp( AfterMin, 0, 255 );
		Filter.Params[4] = NMath::Clamp( AfterMax, 0, 255 );
		return Filter;
	}

	// 2 階調化。明るさ（0..255）が Threshold より低い画素は LowColor に、そうでない画素は HighColor にする（色の A は、結果のアルファ。もとのアルファは使わない）
	static FGraphFilter TwoColor( int32 Threshold, const FColor32& LowColor, const FColor32& HighColor, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::TwoColor, bPremultiplied );
		Filter.Params[0] = NMath::Clamp( Threshold, 0, 255 );
		Filter.Params[1] = PackRgb( LowColor );
		Filter.Params[2] = LowColor.a;
		Filter.Params[3] = PackRgb( HighColor );
		Filter.Params[4] = HighColor.a;
		return Filter;
	}

	// グラデーションマップ。画素の明るさから、Map（横の並びを使う。縦は 1 ピクセルでよい）の x 座標を決めて、その色にする。bReverse で、マップを左右反転して使う。
	// Map は画像のハンドル（IImage から FImageHandle へ）。無効なら、IsValid が false（ApplyFilter は何も記録しない）
	static FGraphFilter GradientMap( FImageHandle Map, bool bReverse = false, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::GradientMap, bPremultiplied );
		Filter.Map = Map;
		Filter.Params[0] = bReverse ? 1 : 0;
		return Filter;
	}

	// 色の置換。FromColor と同じ（RGBA がすべて同じ）画素を、ToColor にする
	static FGraphFilter Replacement( const FColor32& FromColor, const FColor32& ToColor, bool bPremultiplied = false )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Replacement, bPremultiplied );
		Filter.Params[0] = FromColor.r;
		Filter.Params[1] = FromColor.g;
		Filter.Params[2] = FromColor.b;
		Filter.Params[3] = FromColor.a;
		Filter.Params[4] = ToColor.r;
		Filter.Params[5] = ToColor.g;
		Filter.Params[6] = ToColor.b;
		Filter.Params[7] = ToColor.a;
		return Filter;
	}

	// 乗算されていないアルファの画像を、乗算済みアルファにする（色にアルファを掛ける）
	static FGraphFilter PremultiplyAlpha()
	{
		return Make( EDrawGraphFilter::PremulAlpha );
	}

	// 乗算済みアルファの画像を、乗算されていないアルファにする（色をアルファで割る）
	static FGraphFilter UnpremultiplyAlpha()
	{
		return Make( EDrawGraphFilter::InterpAlpha );
	}

	// バイキュービックで、Width x Height へ拡大・縮小する。FilterBlt で、その大きさの Dest へ出す（1..16384）
	static FGraphFilter BicubicScale( int32 Width, int32 Height )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::BicubicScale );
		Filter.Params[0] = NMath::Clamp( Width, 1, 16384 );
		Filter.Params[1] = NMath::Clamp( Height, 1, 16384 );
		return Filter;
	}

	// Lanczos-3 で、Width x Height へ拡大・縮小する（鋭いが、縁にわずかな揺れが出る）。Dest の大きさは、BicubicScale と同じ
	static FGraphFilter Lanczos3Scale( int32 Width, int32 Height )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::Lanczos3Scale );
		Filter.Params[0] = NMath::Clamp( Width, 1, 16384 );
		Filter.Params[1] = NMath::Clamp( Height, 1, 16384 );
		return Filter;
	}

	// 色の成分ごとに、PreSub（0..1 の割合）を引いてから、Scale を掛ける（結果は 0..1 に収まる）。Scale が 1・PreSub が 0 なら、その成分は変わらない
	static FGraphFilter FloatColorScale( float32 ScaleR, float32 ScaleG, float32 ScaleB, float32 ScaleA, float32 PreSubR = 0.0f, float32 PreSubG = 0.0f, float32 PreSubB = 0.0f, float32 PreSubA = 0.0f )
	{
		FGraphFilter Filter = Make( EDrawGraphFilter::FloatColorScale );
		const float32 Values[8] = { ScaleR, ScaleG, ScaleB, ScaleA, PreSubR, PreSubG, PreSubB, PreSubA };
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			// 0 割りやはみ出しを避けるため、有限の値（NaN は 0）に収める
			const float32 Value = Values[Index] == Values[Index] ? NMath::Clamp( Values[Index], -1000.0f, 1000.0f ) : 0.0f;
			FMemory::Memcpy( &Filter.Params[Index], &Value, sizeof( float32 ) );
		}
		return Filter;
	}

	// Params に入っている float の値（FloatColorScale の、引数の順）。確認・実行先が使う
	FORCEINLINE float32 GetFloatParam( int32 Index ) const
	{
		float32 Value = 0.0f;
		if ( Index >= 0 && Index < 8 )
		{
			FMemory::Memcpy( &Value, &Params[Index], sizeof( float32 ) );
		}
		return Value;
	}

private:
	static FORCEINLINE FGraphFilter Make( EDrawGraphFilter Type, bool bPremultiplied = false )
	{
		FGraphFilter Filter;
		Filter.Type = Type;
		Filter.bPremultiplied = bPremultiplied;
		return Filter;
	}

	// 色を、0xRRGGBB の値にする（GetColor が返す値と同じ並び）
	static FORCEINLINE int32 PackRgb( const FColor32& Color )
	{
		return ( static_cast<int32>( Color.r ) << 16 ) | ( static_cast<int32>( Color.g ) << 8 ) | static_cast<int32>( Color.b );
	}

	// 度を、0..359 に収める（負の値も）
	static FORCEINLINE int32 WrapDegrees( int32 Degrees )
	{
		const int32 Wrapped = Degrees % 360;
		return Wrapped < 0 ? Wrapped + 360 : Wrapped;
	}
};

//=============================================================================
// FGraphBlend
//   2 枚の画像（元の画像 Base と、混ぜる画像 Blend）を混ぜて、画像へ出す設定（GraphBlendBlt）。
//   Ratio は混ぜる強さ（0..255。255 でそのまま、0 で元の画像のまま）。範囲の外は収める。
//   Type ごとの結果は、DxLib の GraphBlend と同じ（色の計算はシェーダーで行われるので、GPU によって 1〜2 の誤差が出ることがある）。
//=============================================================================
struct FGraphBlend
{
	EDrawGraphBlend Type = EDrawGraphBlend::Normal;
	int32 Ratio = 255;
	// 乗算済みアルファの画像用の版を使うか（DX_GRAPH_BLEND_PMA_*）
	bool bPremultiplied = false;
	// RgbaSelectMix のときの、出力の R / G / B / A になる成分
	EDrawRgbaSelect Select[4] = { EDrawRgbaSelect::SrcR, EDrawRgbaSelect::SrcG, EDrawRgbaSelect::SrcB, EDrawRgbaSelect::SrcA };

	// 使える値か（種類と、RgbaSelectMix の成分が、範囲の中）
	FORCEINLINE bool IsValid() const
	{
		const int32 Value = static_cast<int32>( Type );
		if ( Value < 0 || Value > static_cast<int32>( EDrawGraphBlend::Mask ) )
		{
			return false;
		}
		for ( const EDrawRgbaSelect Component : Select )
		{
			const int32 Index = static_cast<int32>( Component );
			if ( Index < 0 || Index > static_cast<int32>( EDrawRgbaSelect::BlendInvA ) )
			{
				return false;
			}
		}
		return true;
	}

	// Type の混ぜ方（RgbaSelectMix 以外。RgbaSelectMix を渡すと、出力は、元の画像の成分のまま）
	static FGraphBlend Make( EDrawGraphBlend Type, int32 Ratio = 255, bool bPremultiplied = false )
	{
		FGraphBlend Blend;
		Blend.Type = Type;
		Blend.Ratio = NMath::Clamp( Ratio, 0, 255 );
		Blend.bPremultiplied = bPremultiplied;
		return Blend;
	}

	// 出力の R / G / B / A を、2 枚の画像の成分から選ぶ（例: 元の画像の RGB と、混ぜる画像の R をアルファにする → SrcR, SrcG, SrcB, BlendR）
	static FGraphBlend SelectMix( EDrawRgbaSelect R, EDrawRgbaSelect G, EDrawRgbaSelect B, EDrawRgbaSelect A, int32 Ratio = 255, bool bPremultiplied = false )
	{
		FGraphBlend Blend = Make( EDrawGraphBlend::RgbaSelectMix, Ratio, bPremultiplied );
		Blend.Select[0] = R;
		Blend.Select[1] = G;
		Blend.Select[2] = B;
		Blend.Select[3] = A;
		return Blend;
	}
};
