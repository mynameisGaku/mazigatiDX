// SPDX-License-Identifier: NOASSERTION
#include "EffectPathGeometry.h"

#include <algorithm>
#include <cmath>

namespace
{
	// 二つの更新形状と、その間の補間率。
	struct FNoiseTime
	{
		// 一つ前の形状を決める更新番号。
		uint32 Previous = 0;
		// 次の形状を決める更新番号。
		uint32 Next = 1;
		// 両端で速度が 0 となる補間率。
		float32 Blend = 0.0f;
	};

	// 三つの座標が有限かを確かめる。
	bool IsFinite( const FVector3& Value )
	{
		return std::isfinite( Value.x ) && std::isfinite( Value.y ) && std::isfinite( Value.z );
	}

	// 時刻を整数へ直接変換せず、更新番号と滑らかな補間率へ分ける。
	FNoiseTime GetNoiseTime( float32 Time, float32 RefreshHz )
	{
		// 負や非数の時刻は最初の形状を使う。
		const float64 Seconds = std::isfinite( Time ) ? std::max( static_cast<float64>( Time ), 0.0 ) : 0.0;
		// 負や非数の更新速度は静止として扱う。
		const float64 Rate = std::isfinite( RefreshHz ) ? std::max( static_cast<float64>( RefreshHz ), 0.0 ) : 0.0;
		// 有限な float 二つの積なので double の範囲内に収まる位相。
		const float64 Phase = Seconds * Rate;
		// 現在の更新区間の整数番号。
		const float64 Frame = std::floor( Phase );
		// 二つの形状の間の進み具合。
		const float32 Fraction = static_cast<float32>( Phase - Frame );
		// 大きな時刻でも整数変換を範囲内に保つ更新状態。
		FNoiseTime Result;
		Result.Previous = static_cast<uint32>( std::fmod( Frame, 4294967296.0 ) );
		Result.Next = Result.Previous + 1u;
		Result.Blend = Fraction * Fraction * ( 3.0f - 2.0f * Fraction );
		return Result;
	}

	// 呼び出しの順番に依存せず、シード・更新番号・点番号から一つの値を作る。
	uint32 HashValue( uint32 Seed, uint32 Frame, uint32 Tag )
	{
		// unsigned の循環を使った、一つの乱数値の状態。
		uint32 Value = Seed ^ ( Frame * 0x9E3779B9u ) ^ ( Tag * 0x85EBCA6Bu ) ^ 0xC2B2AE35u;
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		return Value ^ ( Value >> 16 );
	}

	// 同じ点の二つの更新状態を滑らかに混ぜ、-1 以上 1 未満の揺れを返す。
	float32 Noise( uint32 Seed, uint32 Tag, const FNoiseTime& Time )
	{
		// 更新前の形状の値。
		const float32 Before = static_cast<float32>( HashValue( Seed, Time.Previous, Tag ) >> 8 ) * ( 2.0f / 16777216.0f ) - 1.0f;
		// 更新後の形状の値。
		const float32 After = static_cast<float32>( HashValue( Seed, Time.Next, Tag ) >> 8 ) * ( 2.0f / 16777216.0f ) - 1.0f;
		return Before + ( After - Before ) * Time.Blend;
	}

	// 線上の位置から、粗い曲がりと細い折れを重ねる。描画の区間数を変えても同じ位置は同じ値になる。
	float32 SpatialNoise( uint32 Seed, uint32 Axis, float32 T, const FNoiseTime& Time )
	{
		// 段ごとに小さくする揺れの強さ。
		float32 Weight = 1.0f;
		// 揺れの強さを揃えるための重みの合計。
		float32 TotalWeight = 0.0f;
		// 大小の曲がりを足し合わせた値。
		float32 Value = 0.0f;
		// 2・4・8・16・32 区間の固定した空間周波数を重ねる段の番号。
		for ( uint32 Level = 0; Level < 5u; ++Level )
		{
			// この段での、線の始点からの区間位置。
			const float32 Coordinate = T * static_cast<float32>( 2u << Level );
			// 揺れの格子の左側の番号。
			const uint32 Cell = static_cast<uint32>( std::floor( Coordinate ) );
			// 格子の二つの値を混ぜる割合。
			const float32 Fraction = Coordinate - static_cast<float32>( Cell );
			// 粗い曲がりは滑らかにし、細い段は線形で小さな折れを残す。
			const float32 Blend = Level < 2u ? Fraction * Fraction * ( 3.0f - 2.0f * Fraction ) : Fraction;
			// 段・横方向・格子位置で重複しない、左側の揺れの識別番号。
			const uint32 Tag = ( Level + 1u ) * 4099u + Cell * 2u + Axis;
			// 格子の左側の値。
			const float32 Left = Noise( Seed, Tag, Time );
			// 格子の右側の値。
			const float32 Right = Noise( Seed, Tag + 2u, Time );
			Value += ( Left + ( Right - Left ) * Blend ) * Weight;
			TotalWeight += Weight;
			Weight *= 0.5f;
		}
		return Value / TotalWeight;
	}

	// 軸に直交した二方向を作る。軸は長さ 1 を受け取る。
	void MakeBasis( const FVector3& Axis, FVector3& Right, FVector3& Up )
	{
		// 軸と平行に近くならない補助の向き。
		const FVector3 Helper = std::fabs( Axis.y ) < 0.95f ? FVector3::Up : FVector3::Right;
		Right = FVector3::Cross( Axis, Helper ).Normalized();
		Up = FVector3::Cross( Right, Axis );
	}

	// 一本の中心線を作り、端点を固定して横方向だけへ揺らす。不正な座標を作った場合は false。
	bool BuildStrip( const FVector3& Start, const FVector3& End, int32 Segments, float32 Amplitude, float32 Width, const FColor32& Color, uint32 Seed, const FNoiseTime& Time, bool bBranch, FEffectPathGeometry::FStrip& Out )
	{
		// 中心線の軸となる差分。
		const FVector3 Delta = End - Start;
		// 主幹または枝の長さ。
		const float32 Length = Delta.Length();
		if ( !IsFinite( Delta ) || !std::isfinite( Length ) || Length <= 0.0f || !std::isfinite( Amplitude ) ) return false;
		// 揺れの一方向。
		FVector3 Right;
		// 揺れのもう一方向。
		FVector3 Up;
		MakeBasis( Delta / Length, Right, Up );
		Out.Points.Reset();
		Out.Points.Reserve( Segments + 1 );
		// 先頭から積み上げる、画像用のワールド距離。
		float32 Distance = 0.0f;
		// 始点から終点までの点番号。
		for ( int32 Index = 0; Index <= Segments; ++Index )
		{
			// 始点を 0、終点を 1 とする位置。
			const float32 T = static_cast<float32>( Index ) / static_cast<float32>( Segments );
			// 両端を固定するための揺れ幅。端点の座標自体は後で入力値を代入する。
			const float32 Envelope = 4.0f * T * ( 1.0f - T );
			// この点の確定した座標と見た目。
			FEffectPathGeometry::FPoint Point;
			Point.Position = Index == 0 ? Start : Index == Segments ? End : Start + Delta * T + ( Right * SpatialNoise( Seed, 0u, T, Time ) + Up * SpatialNoise( Seed, 1u, T, Time ) ) * ( Amplitude * Envelope );
			Point.Width = bBranch ? Width * ( 1.0f - T ) : Width;
			Point.Color = Color;
			if ( bBranch ) Point.Color.a = static_cast<uint8>( std::lround( static_cast<float32>( Color.a ) * ( 1.0f - T ) ) );
			if ( Index > 0 ) Distance += ( Point.Position - Out.Points.Last().Position ).Length();
			Point.U = Distance;
			if ( !IsFinite( Point.Position ) || !std::isfinite( Point.Width ) || !std::isfinite( Point.U ) ) return false;
			Out.Points.Add( Point );
		}
		return true;
	}
}

void NEffectPath::BuildLightning( const FEffectPathDefinition& Definition, uint32 Seed, float32 Time, const FVector3& Start, const FVector3& End, float32 Width, const FColor32& Color, FEffectPathGeometry& Out )
{
	Out.Strips.Reset();
	if ( Definition.Kind != FEffectPathDefinition::EKind::Lightning || !IsFinite( Start ) || !IsFinite( End ) || !std::isfinite( Width ) || Width <= 0.0f ) return;
	// 主幹の軸となる、ワールドの両端の差。
	const FVector3 Delta = End - Start;
	// 幅の比率を実際の距離へ変えるための、主幹の長さ。
	const float32 Length = Delta.Length();
	if ( !IsFinite( Delta ) || !std::isfinite( Length ) || Length <= 0.0f ) return;
	// 不正な定義を直接渡されても、点数を上限内に保つ区間数。
	const int32 Segments = std::clamp( Definition.Segments, 2, FEffectPathDefinition::MaxSegments );
	// 揺れの比率を、ワールド距離へ変えた幅。
	const float32 Amplitude = ( std::isfinite( Definition.Jitter ) ? std::max( Definition.Jitter, 0.0f ) : 0.0f ) * Length;
	// 枝の比率を、ワールド距離へ変えた長さ。
	const float32 BranchLength = ( std::isfinite( Definition.BranchLength ) ? std::max( Definition.BranchLength, 0.0f ) : 0.0f ) * Length;
	if ( !std::isfinite( Amplitude ) || !std::isfinite( BranchLength ) ) return;
	// 今回の再生時刻で使う、二つの更新番号と補間率。
	const FNoiseTime NoiseTime = GetNoiseTime( Time, Definition.RefreshHz );
	// 生成順で先頭になる、両端の固定された主幹。
	FEffectPathGeometry::FStrip Main;
	if ( !BuildStrip( Start, End, Segments, Amplitude, Width, Color, Seed, NoiseTime, false, Main ) ) return;
	Out.Strips.Add( std::move( Main ) );
	// 長さ 0 の枝は作らず、それ以外は設定数を安全な範囲にする。
	const int32 Branches = BranchLength > 0.0f ? std::clamp( Definition.Branches, 0, FEffectPathDefinition::MaxBranches ) : 0;
	// 枝を横へ伸ばす一方向。
	FVector3 Right;
	// 枝を横へ伸ばすもう一方向。
	FVector3 Up;
	MakeBasis( Delta / Length, Right, Up );
	// 主幹の途中から作る枝の番号。
	for ( int32 Branch = 0; Branch < Branches; ++Branch )
	{
		// 区間数を変えても枝の位置が大きく飛ばない、シードで決まる幹上の割合。
		const float32 AttachmentT = 0.1f + static_cast<float32>( HashValue( Seed, 0u, static_cast<uint32>( Branch ) + 991u ) >> 8 ) * ( 0.8f / 16777216.0f );
		// 主幹の両端を避け、その割合に最も近い主幹点へ枝を接続する。
		const int32 Attachment = std::clamp( static_cast<int32>( std::lround( AttachmentT * static_cast<float32>( Segments ) ) ), 1, Segments - 1 );
		// 生成中の主幹の座標をそのまま共有する、枝の付け根。
		const FVector3 Origin = Out.Strips[0].Points[Attachment].Position;
		// 枝ごとに、主幹と違う形状を作るシード。
		const uint32 BranchSeed = HashValue( Seed, 17u, static_cast<uint32>( Branch ) + 1u );
		// 軸方向の成分を残し、補間途中に向きが反転しない枝の方向。
		const FVector3 Direction = ( Delta / Length * 0.35f + Right * Noise( BranchSeed, 73u, NoiseTime ) + Up * Noise( BranchSeed, 74u, NoiseTime ) ).Normalized();
		// 大きなワールド座標では、ごく短い枝が同じ座標へ丸まる場合だけ枝を省く。
		const FVector3 Tip = Origin + Direction * BranchLength;
		if ( Tip == Origin ) continue;
		// 幹と同じ時刻の揺れを使うが、独立した一点列として描く枝。
		FEffectPathGeometry::FStrip Strip;
		if ( !BuildStrip( Origin, Tip, std::max( Segments / 3, 2 ), Amplitude * 0.35f, Width * 0.55f, Color, BranchSeed, NoiseTime, true, Strip ) )
		{
			Out.Strips.Reset();
			return;
		}
		Out.Strips.Add( std::move( Strip ) );
	}
}
