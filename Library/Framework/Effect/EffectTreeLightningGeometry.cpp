// SPDX-License-Identifier: NOASSERTION
#include "EffectPathGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

namespace
{
	// 完成形の一本と、親からの成長開始時刻をまとめて持つ。
	struct FTreeLightningNode
	{
		// 成長や放電で切り取る前の完成中心線。
		FEffectPathGeometry::FStrip Strip;
		// 枝の根元から成長を始める秒数。
		float64 StartSeconds = 0.0;
		// 枝先まで成長し終える秒数。
		float64 EndSeconds = 0.0;
	};

	// 座標・幅・時刻の計算に使う有限値判定。
	bool IsFinite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}

	// seedと階層内番号から呼び出し順に依存しない32bit値を作る。
	uint32 HashValue( uint32 Seed, uint32 Tag )
	{
		// unsigned循環演算だけで混ぜる状態。
		uint32 Value = Seed ^ ( Tag * 0x9E3779B9u ) ^ 0xA511E9B3u;
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		return Value ^ ( Value >> 16 );
	}

	// hash値から対称な範囲 -1..1 のずれを返す。
	float32 SignedNoise( uint32 Seed, uint32 Tag )
	{
		return static_cast<float32>( HashValue( Seed, Tag ) >> 8 ) * ( 2.0f / 16777216.0f ) - 1.0f;
	}

	// hashから0以上1未満の安定した比率を返す。
	float32 UnitNoise( uint32 Seed, uint32 Tag )
	{
		return static_cast<float32>( HashValue( Seed, Tag ) >> 8 ) * ( 1.0f / 16777216.0f );
	}

	// 線分方向に直交し、左右へ枝を振る安定した基底を作る。
	void MakeBasis( const FVector3& Axis, FVector3& Side, FVector3& Depth )
	{
		// 正面から見た左右を優先し、主軸と平行なら上方向へ切り替える。
		const FVector3 PreferredNormal = NMath::Abs( FVector3::Dot( Axis, FVector3::Forward ) ) < 0.9f ? FVector3::Forward : FVector3::Up;
		Side = FVector3::Cross( PreferredNormal, Axis ).Normalized();
		Depth = FVector3::Cross( Axis, Side ).Normalized();
	}

	// 区間を整数点で再帰二分し、上限付きの折線ずれを決定論的に加える。
	void DisplaceMidpoints( TArray<FVector3>& Positions, int32 First, int32 Last, int32 RootSegments, const FVector3& Side, const FVector3& Depth, float32 Amplitude, uint32 Seed, uint32 Level )
	{
		if ( Last - First < 2 ) return;
		// 既存整数点だけを使う中点で、分割を一定にする。
		const int32 Middle = First + ( Last - First ) / 2;
		if ( Middle <= First || Middle >= Last ) return;
		// 区間番号から階層ごとに独立した乱れを作る。
		const uint32 Tag = Level * 0x10001u + static_cast<uint32>( First ) * 257u + static_cast<uint32>( Last ) * 17u;
		// 横方向は必ず一定以上の強さを持ち、seedで左右を決める。
		const float32 SideRandom = SignedNoise( Seed, Tag + 1u );
		// seedごとの横ずれの絶対量を0.35..1.0へ収める。
		const float32 SideMagnitude = 0.35f + NMath::Abs( SideRandom ) * 0.65f;
		// hash符号を横ずれの向きへ使う。
		const float32 SideComponent = SideRandom < 0.0f ? -SideMagnitude : SideMagnitude;
		// 奥行き方向は横方向より弱くし、立体感だけを加える。
		const float32 DepthComponent = SignedNoise( Seed, Tag + 2u ) * 0.18f;
		// seedごとの大きさを残した乱れ方向の元ベクトル。
		const FVector3 RawNoiseDirection = Side * SideComponent + Depth * DepthComponent;
		// 元ベクトルが単位長を超える時だけ調べる長さ。
		const float32 NoiseLength = RawNoiseDirection.Length();
		// 乱れ方向の長さを最大1に抑え、seedが選んだ弱さは保つ。
		const FVector3 NoiseDirection = NoiseLength > 1.0f ? RawNoiseDirection / NoiseLength : RawNoiseDirection;
		// 区間長に比例し、全階層の変位上限が元のchord長×Jitterを超えない量。
		const float32 DisplacementLimit = 0.5f * Amplitude * static_cast<float32>( Last - First ) / static_cast<float32>( RootSegments );
		// 分割線の中点を基準に両端固定の折れを加える。
		Positions[Middle] = ( Positions[First] + Positions[Last] ) * 0.5f + NoiseDirection * DisplacementLimit;
		DisplaceMidpoints( Positions, First, Middle, RootSegments, Side, Depth, Amplitude, Seed, Level + 1u );
		DisplaceMidpoints( Positions, Middle, Last, RootSegments, Side, Depth, Amplitude, Seed, Level + 1u );
	}

	// 固定端点の折線中心を作り、位置と幅・色・累積距離を帯点へ詰める。
	bool BuildNodeStrip( const FVector3& Start, const FVector3& End, int32 Segments, float32 Jitter, float32 RootWidth, uint8 RootAlpha, const FColor32& Color, uint32 Seed, float32 UBase, bool bTaper, FEffectPathGeometry::FStrip& Out )
	{
		// このnodeの端点間差と長さ。
		const FVector3 Delta = End - Start;
		const float32 Length = Delta.Length();
		if ( !IsFinite( Start ) || !IsFinite( End ) || !IsFinite( Delta ) || !NMath::IsFinite( Length ) || Length <= 1.0e-6f || !NMath::IsFinite( RootWidth ) || RootWidth <= 0.0f || !NMath::IsFinite( UBase ) ) return false;
		// nodeの軸と、その左右・奥行き方向。
		const FVector3 Axis = Delta / Length;
		FVector3 Side;
		FVector3 Depth;
		MakeBasis( Axis, Side, Depth );
		// 任意区間数でも整数再帰で全点を確定する位置配列。
		TArray<FVector3> Positions;
		Positions.SetNum( Segments + 1 );
		for ( int32 Index = 0; Index <= Segments; ++Index ) Positions[Index] = Start + Delta * ( static_cast<float32>( Index ) / static_cast<float32>( Segments ) );
		Positions[0] = Start;
		Positions[Segments] = End;
		DisplaceMidpoints( Positions, 0, Segments, Segments, Side, Depth, Length * Jitter, Seed, 0u );
		Out.Points.Reset();
		Out.Points.Reserve( Segments + 1 );
		// node始点を親のUへ合わせるための累積距離。
		float32 Distance = UBase;
		for ( int32 Index = 0; Index <= Segments; ++Index )
		{
			// この点のnode内進行率。
			const float32 T = static_cast<float32>( Index ) / static_cast<float32>( Segments );
			// nodeの座標、幅、raw RGB、進行に沿って消えるalphaとU。
			FEffectPathGeometry::FPoint Point;
			Point.Position = Index == 0 ? Start : Index == Segments ? End : Positions[Index];
			Point.Width = bTaper ? RootWidth * ( 1.0f - T ) : RootWidth;
			Point.Color = Color;
			Point.Color.a = bTaper ? static_cast<uint8>( std::lround( static_cast<float64>( RootAlpha ) * ( 1.0 - static_cast<float64>( T ) ) ) ) : RootAlpha;
			if ( Index > 0 ) Distance += ( Point.Position - Positions[Index - 1] ).Length();
			Point.U = Distance;
			if ( !IsFinite( Point.Position ) || !NMath::IsFinite( Point.Width ) || !NMath::IsFinite( Point.U ) ) return false;
			Out.Points.Add( Point );
		}
		return true;
	}

	// 点間で位置・幅・alpha・Uを同じ割合で補間する成長先端を作る。
	FEffectPathGeometry::FPoint InterpolatePoint( const FEffectPathGeometry::FPoint& A, const FEffectPathGeometry::FPoint& B, float32 T )
	{
		// 共通の補間率で得る描画点。
		FEffectPathGeometry::FPoint Point;
		Point.Position = FVector3::Lerp( A.Position, B.Position, T );
		Point.Width = A.Width + ( B.Width - A.Width ) * T;
		Point.Color = A.Color;
		Point.Color.a = static_cast<uint8>( NMath::Clamp( static_cast<int32>( std::lround( static_cast<float32>( A.Color.a ) + ( static_cast<float32>( B.Color.a ) - A.Color.a ) * T ) ), 0, 255 ) );
		Point.U = A.U + ( B.U - A.U ) * T;
		return Point;
	}

	// 端の未到達部分を省き、現在の成長位置だけでstripを作る。
	bool ClipGrowingStrip( const FTreeLightningNode& Node, float64 Age, float64 Speed, FEffectPathGeometry::FStrip& Out )
	{
		Out.Points.Reset();
		if ( Node.Strip.Points.Num() < 2 || Age < Node.StartSeconds ) return false;
		for ( int32 Index = 0; Index < Node.Strip.Points.Num(); ++Index )
		{
			// 現在点まで先端が到達する時刻。
			const float64 Arrival = Speed > 0.0 ? Node.StartSeconds + static_cast<float64>( Node.Strip.Points[Index].U - Node.Strip.Points[0].U ) / Speed : Node.StartSeconds;
			if ( Arrival <= Age )
			{
				Out.Points.Add( Node.Strip.Points[Index] );
				continue;
			}
			if ( Index == 0 ) return false;
			// 今の先端が入る区間の到着時刻。
			const float64 PreviousArrival = Speed > 0.0 ? Node.StartSeconds + static_cast<float64>( Node.Strip.Points[Index - 1].U - Node.Strip.Points[0].U ) / Speed : Node.StartSeconds;
			const float64 Span = Arrival - PreviousArrival;
			if ( Span > 0.0 && Age > PreviousArrival )
			{
				// 現在の先端位置を同じ距離割合で補間する。
				const float32 T = static_cast<float32>( NMath::Clamp( ( Age - PreviousArrival ) / Span, 0.0, 1.0 ) );
				Out.Points.Add( InterpolatePoint( Node.Strip.Points[Index - 1], Node.Strip.Points[Index], T ) );
			}
			break;
		}
		return Out.Points.Num() >= 2;
	}

	// alphaだけへ全体のLeader倍率を適用し、RGBは未加工で残す。
	void ScaleAlpha( FEffectPathGeometry::FStrip& Strip, float32 Scale )
	{
		for ( FEffectPathGeometry::FPoint& Point : Strip.Points ) Point.Color.a = static_cast<uint8>( NMath::Clamp( static_cast<int32>( std::lround( static_cast<float32>( Point.Color.a ) * Scale ) ), 0, 255 ) );
	}
}

void NEffectPath::BuildTreeLightning( const FEffectPathDefinition& Definition, uint32 Seed, float32 Age, const FVector3& Start, const FVector3& End, float32 Width, const FColor32& Color, FEffectPathGeometry& Out )
{
	Out.Strips.Reset();
	if ( Definition.Kind != FEffectPathDefinition::EKind::TreeLightning || !NMath::IsFinite( Age ) || Age < 0.0f || !IsFinite( Start ) || !IsFinite( End ) || !NMath::IsFinite( Width ) || Width <= 0.0f ) return;
	// 始点と終点の間の有限な主幹長。
	const FVector3 MainDelta = End - Start;
	const float32 MainLength = MainDelta.Length();
	if ( !IsFinite( MainDelta ) || !NMath::IsFinite( MainLength ) || MainLength <= 1.0e-6f || !NMath::IsFinite( Definition.Jitter ) || Definition.Jitter < 0.0f || Definition.Jitter > 1.0f || !NMath::IsFinite( Definition.BranchLength ) || Definition.BranchLength < 0.0f || Definition.BranchLength > 2.0f || !Definition.Tree.Validate().IsEmpty() ) return;
	// 範囲外の直接入力でも点数と再帰深度を制限する設定。
	const int32 RootSegments = NMath::Clamp( Definition.Segments, 2, FEffectPathDefinition::MaxSegments );
	const int32 PrimarySegments = NMath::Max( RootSegments / 3, 2 );
	const int32 SecondarySegments = NMath::Max( PrimarySegments / 3, 2 );
	const int32 BranchCount = NMath::Clamp( Definition.Branches, 0, FEffectPathDefinition::MaxBranches );
	const int32 ChildCount = NMath::Clamp( Definition.Tree.ChildBranches, 0, 4 );
	const int32 Depth = NMath::Clamp( Definition.Tree.Depth, 0, 2 );
	// 保存済み角度を安全な主枝基準角へ変換する。
	const float64 BaseAngle = NMath::Clamp( static_cast<float64>( Definition.Tree.BranchAngleDegrees ), 5.0, 85.0 ) * 0.017453292519943295;
	const float32 Growth = NMath::Clamp( Definition.Tree.GrowthSeconds, 0.0f, 10.0f );
	const float32 Delay = NMath::Clamp( Definition.Tree.BranchDelaySeconds, 0.0f, 1.0f );
	const float32 Interval = NMath::Clamp( Definition.Tree.StrokeInterval, 0.001f, 2.0f );
	const float32 StrokeLength = NMath::Clamp( Definition.Tree.StrokeSeconds, 0.0001f, Interval );
	const int32 StrokeCount = NMath::Clamp( Definition.Tree.StrokeCount, 1, 8 );
	float64 Speed = 0.0;
	// 主幹と全ての枝をまず完成させ、Ageによるclipより先に予算を確認する。
	TArray<FTreeLightningNode> Nodes;
	Nodes.Reserve( 1 + BranchCount + BranchCount * ChildCount );
	FTreeLightningNode Main;
	Main.StartSeconds = 0.0;
	if ( !BuildNodeStrip( Start, End, RootSegments, Definition.Jitter, Width, Color.a, Color, Seed, 0.0f, false, Main.Strip ) ) return;
	Speed = Growth > 0.0f ? static_cast<float64>( Main.Strip.Points.Last().U ) / Growth : 0.0;
	Main.EndSeconds = Growth;
	Nodes.Add( MoveTemp( Main ) );
	for ( int32 Branch = 0; Branch < BranchCount && Depth >= 1; ++Branch )
	{
		// hashから主幹内側20..80%の整数頂点を選ぶ。
		const int32 AttachMin = NMath::Max( 1, ( RootSegments + 4 ) / 5 );
		const int32 AttachMax = NMath::Min( RootSegments - 1, ( RootSegments * 4 ) / 5 );
		const int32 AttachRange = NMath::Max( AttachMax - AttachMin + 1, 1 );
		const int32 Attachment = AttachMin + static_cast<int32>( HashValue( Seed, static_cast<uint32>( Branch ) + 1u ) % static_cast<uint32>( AttachRange ) );
		// 枝の根元と親の接線方向。
		const FVector3 Origin = Nodes[0].Strip.Points[Attachment].Position;
		const FVector3 Tangent = ( Nodes[0].Strip.Points[Attachment + 1].Position - Nodes[0].Strip.Points[Attachment - 1].Position ).Normalized();
		FVector3 LocalSide;
		FVector3 LocalDepth;
		MakeBasis( Tangent, LocalSide, LocalDepth );
		// 枝ごとに左右を分け、親接線からhashで変化する角度だけ傾ける。
		const float32 BranchSide = ( HashValue( Seed, 0x10000u ) & 1u ) == 0u ? -1.0f : 1.0f;
		const float32 Sign = BranchSide * ( Branch % 2 == 0 ? 1.0f : -1.0f );
		// 枝ごとの角度倍率を0.8..1.2へ収める。
		const float64 BranchAngle = NMath::Clamp( BaseAngle * ( 0.8 + static_cast<float64>( UnitNoise( Seed, 0x11000u + static_cast<uint32>( Branch ) ) ) * 0.4 ), 5.0 * 0.017453292519943295, 85.0 * 0.017453292519943295 );
		// 枝の立体角を設定角と独立して±0.16radへ収める。
		const float32 BranchDepthAngle = SignedNoise( Seed, 0x12000u + static_cast<uint32>( Branch ) ) * 0.16f;
		// 横・奥行き面で単位長となる枝の側方向。
		const FVector3 BranchLateral = LocalSide * ( Sign * static_cast<float32>( std::cos( BranchDepthAngle ) ) ) + LocalDepth * static_cast<float32>( std::sin( BranchDepthAngle ) );
		// 設定角度を親接線との実角として保つ枝方向。
		const FVector3 Direction = ( Tangent * static_cast<float32>( std::cos( BranchAngle ) ) + BranchLateral * static_cast<float32>( std::sin( BranchAngle ) ) ).Normalized();
		// 主幹の接続点から引き継ぐ全幅。
		const float32 ParentWidth = Nodes[0].Strip.Points[Attachment].Width;
		// 主幹の接続点から引き継ぐalpha。
		const uint8 ParentAlpha = Nodes[0].Strip.Points[Attachment].Color.a;
		// 一次枝の長さへseed固有の0.55..1.0倍率を加える。
		const float32 PrimaryLengthScale = 0.55f + UnitNoise( Seed, 0x13000u + static_cast<uint32>( Branch ) ) * 0.45f;
		// 主幹chordと設定値へ倍率を掛けた一次枝長。
		const float32 BranchLength = MainLength * Definition.BranchLength * PrimaryLengthScale;
		FTreeLightningNode Primary;
		Primary.StartSeconds = Speed > 0.0 ? static_cast<float64>( Nodes[0].Strip.Points[Attachment].U ) / Speed + Delay : Delay;
		if ( BranchLength > 1.0e-6f && !BuildNodeStrip( Origin, Origin + Direction * BranchLength, PrimarySegments, Definition.Jitter, ParentWidth * 0.55f, ParentAlpha, Color, HashValue( Seed, 0x20000u + static_cast<uint32>( Branch ) ), Nodes[0].Strip.Points[Attachment].U, true, Primary.Strip ) ) return;
		if ( BranchLength <= 1.0e-6f ) continue;
		Primary.EndSeconds = Primary.StartSeconds + ( Speed > 0.0 ? static_cast<float64>( Primary.Strip.Points.Last().U - Primary.Strip.Points[0].U ) / Speed : 0.0 );
		if ( Nodes.Num() + Primary.Strip.Points.Num() > FEffectPathGeometry::MaxPoints ) return;
		Nodes.Add( MoveTemp( Primary ) );
		const int32 PrimaryIndex = Nodes.Num() - 1;
		for ( int32 Child = 0; Child < ChildCount && Depth >= 2; ++Child )
		{
			// 子枝を親枝の内側20..80%へ接続する整数頂点。
			const int32 ChildAttachMin = NMath::Max( 1, ( PrimarySegments + 4 ) / 5 );
			const int32 ChildAttachMax = NMath::Min( PrimarySegments - 1, ( PrimarySegments * 4 ) / 5 );
			const int32 ChildAttachRange = NMath::Max( ChildAttachMax - ChildAttachMin + 1, 1 );
			const int32 ChildAttachment = ChildAttachMin + static_cast<int32>( HashValue( Seed, 0x30000u + static_cast<uint32>( Branch * 7 + Child ) ) % static_cast<uint32>( ChildAttachRange ) );
			const FEffectPathGeometry::FStrip& ParentStrip = Nodes[PrimaryIndex].Strip;
			const FVector3 ChildOrigin = ParentStrip.Points[ChildAttachment].Position;
			const FVector3 ChildTangent = ( ParentStrip.Points[ChildAttachment + 1].Position - ParentStrip.Points[ChildAttachment - 1].Position ).Normalized();
			FVector3 ChildSide;
			FVector3 ChildDepth;
			MakeBasis( ChildTangent, ChildSide, ChildDepth );
			const float32 ChildInitialSign = ( HashValue( Seed, 0x40000u + static_cast<uint32>( Branch ) ) & 1u ) == 0u ? -1.0f : 1.0f;
			const float32 ChildSign = ChildInitialSign * ( Child % 2 == 0 ? 1.0f : -1.0f );
		// 二次枝ごとに独立した角度と長さを選ぶhash番号。
			const uint32 ChildTag = 0x41000u + static_cast<uint32>( Branch * 7 + Child );
			// 二次枝の角度倍率を0.8..1.2へ収める。
			const float64 ChildAngle = NMath::Clamp( BaseAngle * ( 0.8 + static_cast<float64>( UnitNoise( Seed, ChildTag ) ) * 0.4 ), 5.0 * 0.017453292519943295, 85.0 * 0.017453292519943295 );
			// 二次枝の立体角を設定角から独立して±0.16radへ収める。
			const float32 ChildDepthAngle = SignedNoise( Seed, ChildTag + 1u ) * 0.16f;
			// 横・奥行き面で単位長となる二次枝の側方向。
			const FVector3 ChildLateral = ChildSide * ( ChildSign * static_cast<float32>( std::cos( ChildDepthAngle ) ) ) + ChildDepth * static_cast<float32>( std::sin( ChildDepthAngle ) );
			// 設定角度を親接線との実角として保つ二次枝方向。
			const FVector3 ChildDirection = ( ChildTangent * static_cast<float32>( std::cos( ChildAngle ) ) + ChildLateral * static_cast<float32>( std::sin( ChildAngle ) ) ).Normalized();
			// 親の実際のchordの半分を上限にseed固有の長さを選ぶ。
			const float32 SecondaryLengthScale = 0.55f + UnitNoise( Seed, ChildTag + 2u ) * 0.45f;
			const float32 ChildLength = BranchLength * 0.5f * SecondaryLengthScale;
			// 接続した一次枝の幅から引き継ぐ二次枝幅。
			const float32 ChildWidth = ParentStrip.Points[ChildAttachment].Width * 0.55f;
			// 接続した一次枝のalphaから引き継ぐ二次枝alpha。
			const uint8 ChildAlpha = ParentStrip.Points[ChildAttachment].Color.a;
			FTreeLightningNode Secondary;
			Secondary.StartSeconds = Nodes[PrimaryIndex].StartSeconds + ( Speed > 0.0 ? static_cast<float64>( ParentStrip.Points[ChildAttachment].U - ParentStrip.Points[0].U ) / Speed : 0.0 ) + Delay;
			if ( !BuildNodeStrip( ChildOrigin, ChildOrigin + ChildDirection * ChildLength, SecondarySegments, Definition.Jitter, ChildWidth, ChildAlpha, Color, HashValue( Seed, 0x50000u + static_cast<uint32>( Branch * 7 + Child ) ), ParentStrip.Points[ChildAttachment].U, true, Secondary.Strip ) ) return;
			Secondary.EndSeconds = Secondary.StartSeconds + ( Speed > 0.0 ? static_cast<float64>( Secondary.Strip.Points.Last().U - Secondary.Strip.Points[0].U ) / Speed : 0.0 );
			if ( Nodes.Num() + Secondary.Strip.Points.Num() > FEffectPathGeometry::MaxPoints ) return;
			Nodes.Add( MoveTemp( Secondary ) );
		}
	}
	// 完成形が許容点数・strip数内に収まることを確かめる。
	int32 TotalPoints = 0;
	float64 GrowthEnd = 0.0;
	for ( const FTreeLightningNode& Node : Nodes )
	{
		TotalPoints += Node.Strip.Points.Num();
		GrowthEnd = NMath::Max( GrowthEnd, Node.EndSeconds );
	}
	if ( TotalPoints > FEffectPathGeometry::MaxPoints || Nodes.Num() > 81 ) return;
	// 成長中はleaderを暗くし、完成後は指定された放電窓だけを明るくする。
	const bool bGrowing = Age < GrowthEnd;
	float32 Brightness = bGrowing ? 0.12f : 0.0f;
	if ( !bGrowing )
	{
		for ( int32 Stroke = 0; Stroke < StrokeCount; ++Stroke )
		{
			// この放電が始まる秒数。
			const float64 StrokeStart = GrowthEnd + static_cast<float64>( Stroke ) * Interval;
			const float64 Elapsed = static_cast<float64>( Age ) - StrokeStart;
			if ( Elapsed >= 0.0 && Elapsed < StrokeLength )
			{
				Brightness = static_cast<float32>( 1.0 - Elapsed / StrokeLength );
				break;
			}
		}
	}
	if ( Brightness <= 0.0f ) return;
	FEffectPathGeometry Built;
	Built.Strips.Reserve( Nodes.Num() );
	for ( const FTreeLightningNode& Node : Nodes )
	{
		// 成長中はnodeごとに先端をclipし、放電中は完成形を複写する。
		FEffectPathGeometry::FStrip Visible;
		const bool bVisible = bGrowing ? ClipGrowingStrip( Node, Age, Speed, Visible ) : ( Visible = Node.Strip, true );
		if ( !bVisible || Visible.Points.Num() < 2 ) continue;
		ScaleAlpha( Visible, Brightness );
		Built.Strips.Add( MoveTemp( Visible ) );
	}
	Out = MoveTemp( Built );
}

bool NEffectPath::GetTreeLightningBounds( const FEffectPathDefinition& Definition, const FVector3& Start, const FVector3& End, float32 Padding, FVector3& OutCenter, float32& OutRadius )
{
	if ( Definition.Kind != FEffectPathDefinition::EKind::TreeLightning || !IsFinite( Start ) || !IsFinite( End ) || !NMath::IsFinite( Padding ) || Padding < 0.0f || !NMath::IsFinite( Definition.Jitter ) || Definition.Jitter < 0.0f || Definition.Jitter > 1.0f || !NMath::IsFinite( Definition.BranchLength ) || Definition.BranchLength < 0.0f || Definition.BranchLength > 2.0f || Definition.Segments < 2 || Definition.Segments > FEffectPathDefinition::MaxSegments || Definition.Branches < 0 || Definition.Branches > FEffectPathDefinition::MaxBranches || !Definition.Tree.Validate().IsEmpty() ) return false;
	// 始点から終点への有限なchordを計算する。
	const FVector3 Delta = End - Start;
	// x成分をdoubleで二乗して長さ丸め誤差を避ける。
	const float64 DeltaX = static_cast<float64>( Delta.x );
	// y成分をdoubleで二乗して長さ丸め誤差を避ける。
	const float64 DeltaY = static_cast<float64>( Delta.y );
	// z成分をdoubleで二乗して長さ丸め誤差を避ける。
	const float64 DeltaZ = static_cast<float64>( Delta.z );
	// float中心とboundsの計算に使う正確なchord長。
	const float64 Length = std::sqrt( DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ );
	if ( !IsFinite( Delta ) || !NMath::IsFinite( Length ) || Length <= 1.0e-6 ) return false;
	// 公開するfloat中心を確定し、その丸めを半径計算へ反映する。
	const FVector3 Center = Start + Delta * 0.5f;
	// float中心の丸め後も両端を含めるため、始点との差をdoubleで測る。
	const float64 StartX = static_cast<float64>( Start.x ) - static_cast<float64>( Center.x );
	// 同じ始点差計算のy成分。
	const float64 StartY = static_cast<float64>( Start.y ) - static_cast<float64>( Center.y );
	// 同じ始点差計算のz成分。
	const float64 StartZ = static_cast<float64>( Start.z ) - static_cast<float64>( Center.z );
	// float中心の丸め後も両端を含めるため、終点との差をdoubleで測る。
	const float64 EndX = static_cast<float64>( End.x ) - static_cast<float64>( Center.x );
	// 同じ終点差計算のy成分。
	const float64 EndY = static_cast<float64>( End.y ) - static_cast<float64>( Center.y );
	// 同じ終点差計算のz成分。
	const float64 EndZ = static_cast<float64>( End.z ) - static_cast<float64>( Center.z );
	// 丸められた中心から始点までの実距離を二乗和で求める。
	const float64 StartRadius = std::sqrt( StartX * StartX + StartY * StartY + StartZ * StartZ );
	// 丸められた中心から終点までの実距離を二乗和で求める。
	const float64 EndRadius = std::sqrt( EndX * EndX + EndY * EndY + EndZ * EndZ );
	// 両端を収めるために必要な最大距離。
	const float64 EndpointRadius = NMath::Max( StartRadius, EndRadius );
	// 深さと枝数に応じて必要な最大枝長だけを加算する。
	const float64 MaximumPrimaryChord = Definition.Tree.Depth >= 1 && Definition.Branches > 0 ? Length * static_cast<float64>( Definition.BranchLength ) : 0.0;
	const float64 MaximumSecondaryChord = Definition.Tree.Depth >= 2 && Definition.Tree.ChildBranches > 0 && Definition.Branches > 0 ? MaximumPrimaryChord * 0.5 : 0.0;
	// 定義で許されるchordからの最大ずれ割合。
	const float64 Jitter = static_cast<float64>( Definition.Jitter );
	// 幹の両端からの距離、幹の乱れ、枝の乱れ、利用者paddingを含む半径。
	const float64 Radius = EndpointRadius + Length * Jitter + MaximumPrimaryChord * ( 1.0 + Jitter ) + MaximumSecondaryChord * ( 1.0 + Jitter ) + static_cast<float64>( Padding );
	// float出力へ安全に収まる値だけを呼び出し側へ渡す。
	if ( !NMath::IsFinite( Radius ) || Radius > static_cast<float64>( TNumericLimits<float32>::Max() ) || !IsFinite( Center ) ) return false;
	OutCenter = Center;
	OutRadius = static_cast<float32>( Radius );
	if ( static_cast<float64>( OutRadius ) < Radius ) OutRadius = std::nextafter( OutRadius, TNumericLimits<float32>::Infinity() );
	return NMath::IsFinite( OutRadius );
}
