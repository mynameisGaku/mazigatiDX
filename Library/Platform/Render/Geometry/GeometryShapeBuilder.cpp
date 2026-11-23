// SPDX-License-Identifier: MIT
#include "GeometryShapeBuilder.h"
#include "Library/Core/Log/Assert.h"
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
	// 円周を Div 分割したときの、Index 番目の境目での円の外向き。最後の点を最初の点へ正確に重ねる。
	FVector3 MakeRadial( const FVector3& Right, const FVector3& Tangent, int32 Index, int32 Div )
	{
		const float32 Angle = Index == Div ? 0 : 2 * NMath::PI * Index / Div;
		return Right * NMath::Cos( Angle ) + Tangent * NMath::Sin( Angle );
	}
	// 円周を Div 分割した全部の境目（最後の点は最初の点と重なる）の、円の外向きを、1 回ずつ作る。
	void MakeRadials( const FVector3& Right, const FVector3& Tangent, int32 Div, TArray<FVector3>& Out )
	{
		Out.Reserve( Div + 1 );
		for ( int32 Index = 0; Index <= Div; ++Index )
		{
			Out.Add( MakeRadial( Right, Tangent, Index, Div ) );
		}
	}
	// 円周上の一点（Radial は MakeRadial の結果）を、頂点へ変える。
	FDrawVertex3D MakeVertex( const FProfileRing& Ring, const FVector3& Axis, const FVector3& Radial, FColor32 Color )
	{
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
	// 頂点の位置が、有限か。
	bool FinitePosition( const FDrawVertex3D& Vertex )
	{
		return NMath::IsFinite( Vertex.Position[0] ) && NMath::IsFinite( Vertex.Position[1] ) && NMath::IsFinite( Vertex.Position[2] );
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
		// 隣り合う面は、同じ頂点を共有する。三角関数を使う頂点の計算は、円ごと・境目ごとに 1 回だけにする。
		const int32 NumColumns = Div + 1; // 境目の数（最後の境目は、最初の境目と重なる）。
		TArray<FVector3> Radials; // 境目ごとの円の外向き。
		MakeRadials( Right, Tangent, Div, Radials );
		TArray<FDrawVertex3D> Grid; // 円ごと・境目ごとの頂点（円の番号 * 境目の数 + 境目の番号）。
		Grid.Reserve( Rings.Num() * NumColumns );
		for ( const FProfileRing& Ring : Rings )
		{
			for ( int32 Column = 0; Column < NumColumns; ++Column )
			{
				Grid.Add( MakeVertex( Ring, Axis, Radials[Column], Color ) );
			}
		}
		int32 NumOutputVertices = 0; // 出力する頂点の数。先に数えて、1 回で確保する。
		for ( int32 Row = 0; Row + 1 < Rings.Num(); ++Row )
		{
			const bool bLowerHasArea = Rings[Row].Radius > 0; // 下の円が、半径を持つか。
			const bool bUpperHasArea = Rings[Row + 1].Radius > 0; // 上の円が、半径を持つか。
			NumOutputVertices += Div * ( bFill ? ( bLowerHasArea ? 3 : 0 ) + ( bUpperHasArea ? 3 : 0 ) : 2 + ( bLowerHasArea ? 2 : 0 ) );
		}
		const int32 FirstOutput = Output.AddUninitialized( NumOutputVertices );
		FDrawVertex3D* Write = Output.GetData() + FirstOutput; // 出力の書き込み位置。
		for ( int32 Row = 0; Row + 1 < Rings.Num(); ++Row )
		{
			const FDrawVertex3D* Lower = Grid.GetData() + Row * NumColumns; // 下の円の頂点。
			const FDrawVertex3D* Upper = Lower + NumColumns; // 上の円の頂点。
			const bool bLowerHasArea = Rings[Row].Radius > 0;
			const bool bUpperHasArea = Rings[Row + 1].Radius > 0;
			for ( int32 Column = 0; Column < Div; ++Column )
			{
				const FDrawVertex3D& A0 = Lower[Column]; // 下の円の始点。
				const FDrawVertex3D& A1 = Lower[Column + 1]; // 下の円の終点。
				const FDrawVertex3D& B0 = Upper[Column]; // 上の円の始点。
				const FDrawVertex3D& B1 = Upper[Column + 1]; // 上の円の終点。
				if ( bFill )
				{
					if ( bLowerHasArea )
					{
						*Write++ = A0;
						*Write++ = A1;
						*Write++ = B0;
					}
					if ( bUpperHasArea )
					{
						*Write++ = A1;
						*Write++ = B1;
						*Write++ = B0;
					}
				}
				else
				{
					*Write++ = A0;
					*Write++ = B0;
					if ( bLowerHasArea )
					{
						*Write++ = A0;
						*Write++ = A1;
					}
				}
			}
		}
		DEBUG_CHECK( Write == Output.GetData() + FirstOutput + NumOutputVertices );
		for ( const FDrawVertex3D& Vertex : Output )
		{
			if ( !FinitePosition( Vertex ) )
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

bool CGeometryShapeBuilder::BuildCone( const FVector3& Top, const FVector3& Bottom, float32 Radius, int32 Div, bool bFill, FColor32 Color, TArray<FDrawVertex3D>& Out )
{
	if ( !Finite( Top ) || !Finite( Bottom ) || !NMath::IsFinite( Radius ) || Radius < 0 || Div < 3 || Div > 128 )
	{
		return false;
	}
	TArray<FDrawVertex3D> Output; // 全座標を検査してから出力へ渡す。
	const FVector3 Offset = Top - Bottom; // 底面の中心から頂点への軸。
	const float32 Height = Offset.Length(); // 正規化前に有限性を調べる。
	if ( !Finite( Offset ) || !NMath::IsFinite( Height ) )
	{
		return false;
	}
	if ( Radius == 0 || Height <= NMath::SmallNumber )
	{
		Out = MoveTemp( Output );
		return true;
	}
	const FVector3 Axis = Offset / Height; // 底面から頂点への向き。
	const FVector3 Reference = NMath::Abs( Axis.y ) < 0.9f ? FVector3::Up : FVector3::Right; // 軸と平行でない基準。
	const FVector3 Right = FVector3::Cross( Reference, Axis ).Normalized(); // 底面の円の1つ目の軸。
	const FVector3 Tangent = FVector3::Cross( Axis, Right ); // 底面の円の2つ目の軸。
	const float32 Slant = NMath::Sqrt( Height * Height + Radius * Radius ); // 側面の母線の長さ。
	const auto Make = [&]( const FVector3& Position, const FVector3& Normal ) // 位置・法線・色を共通頂点へ移す。
	{
		FDrawVertex3D Result;
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
	};
	TArray<FVector3> Radials; // 円周上の、外向きの単位ベクトル（最後の点は、最初の点と正確に重なる）。隣り合う分割で共有するので、1 回ずつだけ作る。
	MakeRadials( Right, Tangent, Div, Radials );
	Output.Reserve( Div * ( bFill ? 6 : 4 ) );
	for ( int32 Index = 0; Index < Div; ++Index )
	{
		const FVector3& Radial0 = Radials[Index]; // この分割の両端の向き。
		const FVector3& Radial1 = Radials[Index + 1];
		const FVector3 Point0 = Bottom + Radial0 * Radius, Point1 = Bottom + Radial1 * Radius; // 底面の円周上の2点。
		if ( !Finite( Point0 ) || !Finite( Point1 ) )
		{
			return false;
		}
		if ( bFill )
		{
			const FVector3 Normal0 = ( Radial0 * Height + Axis * Radius ) / Slant, Normal1 = ( Radial1 * Height + Axis * Radius ) / Slant; // 側面の外向きの法線。
			const FVector3 NormalTop = ( Normal0 + Normal1 ).Normalized(); // 頂点は、両隣の法線の中間。
			// 側面（外から見て時計回り）。
			Output.Add( Make( Top, NormalTop ) );
			Output.Add( Make( Point1, Normal1 ) );
			Output.Add( Make( Point0, Normal0 ) );
			// 底面（下から見て時計回り）。
			Output.Add( Make( Bottom, Axis * -1.0f ) );
			Output.Add( Make( Point0, Axis * -1.0f ) );
			Output.Add( Make( Point1, Axis * -1.0f ) );
		}
		else
		{
			Output.Add( Make( Point0, Axis ) );
			Output.Add( Make( Point1, Axis ) );
			Output.Add( Make( Top, Axis ) );
			Output.Add( Make( Point0, Axis ) );
		}
	}
	Out = MoveTemp( Output );
	return true;
}
