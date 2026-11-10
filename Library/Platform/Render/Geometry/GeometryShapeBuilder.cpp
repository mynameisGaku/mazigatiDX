// SPDX-License-Identifier: MIT
#include "GeometryShapeBuilder.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 軸の周りに置く円。両端の極は半径0で表す。
	struct FProfileRing
	{
		FVector3 Center; // 円の中心。
		float32 Radius = 0; // 軸からの距離。
		float32 NormalAxis = 0, NormalRadial = 0; // 法線の軸方向と円方向の成分。
	};
	// 位置と演算結果の非有限値を拒否する。
	bool Finite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}
	// 円周上の一点を、継ぎ目が一致する頂点へ変える。
	FDrawVertex3D MakeVertex( const FProfileRing& Ring, const FVector3& Axis, const FVector3& Right, const FVector3& Tangent, int32 Index, int32 Div, FColor32 Color )
	{
		const float32 Angle = Index == Div ? 0 : 2 * NMath::PI * Index / Div; // 最後の点を最初の点へ正確に重ねる。
		const FVector3 Radial = Right * NMath::Cos( Angle ) + Tangent * NMath::Sin( Angle ); // 円の外向き。
		const FVector3 Position = Ring.Center + Radial * Ring.Radius; // ローカル位置。
		const FVector3 Normal = Axis * Ring.NormalAxis + Radial * Ring.NormalRadial; // 単位法線。
		FDrawVertex3D Result; // 照明なしでは拡散色をそのまま使う。
		Result.Position[0] = Position.x;
		Result.Position[1] = Position.y;
		Result.Position[2] = Position.z;
		Result.Normal[0] = Normal.x;
		Result.Normal[1] = Normal.y;
		Result.Normal[2] = Normal.z;
		Result.Diffuse[0] = Color.r;
		Result.Diffuse[1] = Color.g;
		Result.Diffuse[2] = Color.b;
		Result.Diffuse[3] = Color.a;
		return Result;
	}
	// 二つの半球と、必要なら円柱をつなぐ。最大分割でも1描画65536頂点以内。
	bool BuildRounded( const FVector3& A, const FVector3& B, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out )
	{
		if ( !Finite( A ) || !Finite( B ) || !NMath::IsFinite( Radius ) || Radius < 0 || Div < 3 || Div > 128 )
		{
			return false;
		}
		TArray<FDrawVertex3D> Output; // 全座標を検査してから出力へ渡す。
		if ( Radius == 0 )
		{
			Out = MoveTemp( Output );
			return true;
		}
		const FVector3 Offset = B - A; // カプセルの軸。
		const float32 Length = Offset.Length(); // 正規化前に有限性を調べる。
		if ( !Finite( Offset ) || !NMath::IsFinite( Length ) )
		{
			return false;
		}
		const bool bSphere = Length <= NMath::SmallNumber; // 零長の軸は安定した球へ戻す。
		const FVector3 Axis = bSphere ? FVector3::Up : Offset / Length; // 半球を並べる方向。
		const FVector3 Reference = NMath::Abs( Axis.y ) < 0.9f ? FVector3::Up : FVector3::Right; // 軸と平行でない基準。
		const FVector3 Right = FVector3::Cross( Reference, Axis ).Normalized(); // 円周の始点方向。
		const FVector3 Tangent = FVector3::Cross( Axis, Right ); // 円周を進む方向。
		const int32 HalfDiv = ( Div + 3 ) / 4; // 半球の緯度数。
		TArray<FProfileRing> Rings; // 下の極から上の極までの円。
		for ( int32 Half = 0; Half < 2; ++Half )
		{
			for ( int32 Step = 0; Step <= HalfDiv; ++Step )
			{
				if ( bSphere && Half == 1 && Step == 0 )
				{
					continue;
				}
				const float32 Angle = ( Half == 0 ? -1.0f + static_cast<float32>( Step ) / HalfDiv : static_cast<float32>( Step ) / HalfDiv ) * NMath::PI * 0.5f; // 半球の緯度。
				const bool bPole = ( Half == 0 && Step == 0 ) || ( Half == 1 && Step == HalfDiv ); // 極には微小な円を残さない。
				FProfileRing Ring; // 位置と法線に同じ緯度を使う。
				Ring.NormalAxis = bPole ? ( Half == 0 ? -1.0f : 1.0f ) : NMath::Sin( Angle );
				Ring.NormalRadial = bPole ? 0 : NMath::Cos( Angle );
				Ring.Center = ( Half == 0 || bSphere ? A : B ) + Axis * ( Radius * Ring.NormalAxis );
				Ring.Radius = Radius * Ring.NormalRadial;
				Rings.Add( Ring );
			}
		}
		Output.Reserve( Div * ( Rings.Num() - 1 ) * ( bFill ? 6 : 4 ) );
		for ( int32 Row = 0; Row + 1 < Rings.Num(); ++Row )
		{
			for ( int32 Column = 0; Column < Div; ++Column )
			{
				const FDrawVertex3D A0 = MakeVertex( Rings[Row], Axis, Right, Tangent, Column, Div, Color ); // 下の円の始点。
				const FDrawVertex3D A1 = MakeVertex( Rings[Row], Axis, Right, Tangent, Column + 1, Div, Color ); // 下の円の終点。
				const FDrawVertex3D B0 = MakeVertex( Rings[Row + 1], Axis, Right, Tangent, Column, Div, Color ); // 上の円の始点。
				const FDrawVertex3D B1 = MakeVertex( Rings[Row + 1], Axis, Right, Tangent, Column + 1, Div, Color ); // 上の円の終点。
				if ( bFill )
				{
					if ( Rings[Row].Radius > 0 )
					{
						Output.Add( A0 );
						Output.Add( A1 );
						Output.Add( B0 );
					}
					if ( Rings[Row + 1].Radius > 0 )
					{
						Output.Add( A1 );
						Output.Add( B1 );
						Output.Add( B0 );
					}
				}
				else
				{
					Output.Add( A0 );
					Output.Add( B0 );
					if ( Rings[Row].Radius > 0 )
					{
						Output.Add( A0 );
						Output.Add( A1 );
					}
				}
			}
		}
		for ( const auto& Vertex : Output )
		{
			if ( !Finite( FVector3( Vertex.Position[0], Vertex.Position[1], Vertex.Position[2] ) ) )
			{
				return false;
			}
		}
		Out = MoveTemp( Output );
		return true;
	}
}

bool CGeometryShapeBuilder::BuildSphere( const FVector3& Center, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out )
{
	return BuildRounded( Center, Center, Radius, Div, bFill, Color, Out );
}

bool CGeometryShapeBuilder::BuildCapsule( const FVector3& A, const FVector3& B, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out )
{
	return BuildRounded( A, B, Radius, Div, bFill, Color, Out );
}
