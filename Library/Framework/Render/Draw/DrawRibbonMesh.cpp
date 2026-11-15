// SPDX-License-Identifier: MIT
#include "DrawRibbonMesh.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 大きな座標差も 64 ビットで求め、同じ位置なら指定の向きを返す。
	FVector3 RibbonDirection( const FVector3& From, const FVector3& To, const FVector3& Fallback )
	{
		// 終点と始点の差を、float の桁あふれの前に広げた X 成分。
		const float64 X = static_cast<float64>( To.x ) - From.x;
		// 同じく Y 成分。
		const float64 Y = static_cast<float64>( To.y ) - From.y;
		// 同じく Z 成分。
		const float64 Z = static_cast<float64>( To.z ) - From.z;
		// 単位化するための 3 次元の距離。
		const float64 Length = NMath::Sqrt( X * X + Y * Y + Z * Z );
		if ( !NMath::IsFinite( Length ) || Length <= 1.0e-8 )
		{
			return Fallback;
		}
		return FVector3( static_cast<float32>( X / Length ), static_cast<float32>( Y / Length ), static_cast<float32>( Z / Length ) );
	}

	// 経路と視線に垂直な幅の方向を求める。平行なときは、カメラの軸から代わりを作る。
	FVector3 RibbonSide( const FVector3& Tangent, const FVector3& ToCamera, const FVector3& CameraRight, const FVector3& CameraUp )
	{
		// 経路と視線に垂直な、通常の幅の方向。
		FVector3 Side = FVector3::Cross( Tangent, ToCamera );
		if ( Side.LengthSquared() < 1.0e-10f )
		{
			Side = CameraRight - Tangent * FVector3::Dot( CameraRight, Tangent );
		}
		if ( Side.LengthSquared() < 1.0e-10f )
		{
			Side = CameraUp - Tangent * FVector3::Dot( CameraUp, Tangent );
		}
		return RibbonDirection( FVector3( 0.0f, 0.0f, 0.0f ), Side, CameraRight );
	}

	// 画像の境界へ足す頂点を補間する。
	FDrawVertex3D RibbonVertexBetween( const FDrawVertex3D& First, const FDrawVertex3D& Last, float64 Time, float32 U )
	{
		// 位置などの連続した値を、桁あふれしにくい精度で補間する。
		const auto Mix = [&]( float32 A, float32 B ) { return static_cast<float32>( static_cast<float64>( A ) + ( static_cast<float64>( B ) - A ) * Time ); };
		// 色の成分を、丸めてバイトの範囲へ収める。
		const auto MixByte = [&]( uint8 A, uint8 B ) { return static_cast<uint8>( NMath::Clamp( Mix( static_cast<float32>( A ), static_cast<float32>( B ) ), 0.0f, 255.0f ) + 0.5f ); };
		// 先頭の頂点を土台にした、切れ目へ入れる頂点。
		FDrawVertex3D Result = First;
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			Result.Position[Axis] = Mix( First.Position[Axis], Last.Position[Axis] );
		}
		const FVector3 Normal = RibbonDirection( FVector3( 0.0f, 0.0f, 0.0f ), FVector3( Mix( First.Normal[0], Last.Normal[0] ), Mix( First.Normal[1], Last.Normal[1] ), Mix( First.Normal[2], Last.Normal[2] ) ),
			FVector3( First.Normal[0], First.Normal[1], First.Normal[2] ) );
		Result.Normal[0] = Normal.x;
		Result.Normal[1] = Normal.y;
		Result.Normal[2] = Normal.z;
		for ( int32 Channel = 0; Channel < 4; ++Channel )
		{
			Result.Diffuse[Channel] = MixByte( First.Diffuse[Channel], Last.Diffuse[Channel] );
		}
		Result.U = U;
		return Result;
	}

	// 画像の繰り返しを、整数の U の境界で分ける。
	bool SplitRibbonTexture( const FDrawRibbonData& Data, FDrawRibbonMesh& Mesh )
	{
		// 分割後の頂点（1 区間に 4 つ）。
		TArray<FDrawVertex3D> SplitVertices;
		// 分割後の三角形の番号。
		TArray<uint16> SplitIndices;
		// これまでに作った、画像の繰り返しの区間の数。
		int32 TotalSections = 0;
		for ( int32 Index = 0; Index + 1 < Data.NumPoints; ++Index )
		{
			if ( ( Index == 0 && ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) || ( Index + 2 == Data.NumPoints && ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) )
			{
				continue;
			}
			// 区間が、画像の座標で始まる位置。
			const float64 StartU = Mesh.Vertices[Index * 2].U;
			// 区間が、画像の座標で終わる位置。
			const float64 EndU = Mesh.Vertices[( Index + 1 ) * 2].U;
			// 区間の、最初の整数の繰り返し番号。
			const float64 FirstRepeat = NMath::Floor( StartU );
			// 区間を整数の境界で分けるのに必要な数。
			const float64 Required = NMath::Max( 1.0, NMath::Ceil( EndU ) - FirstRepeat );
			if ( !NMath::IsFinite( Required ) || Required > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TotalSections ) )
			{
				return false;
			}
			// 上限を確かめてから整数にした、区間の繰り返しの数。
			const int32 Sections = static_cast<int32>( Required );
			TotalSections += Sections;
			// 1 つ前の小さな区間が終わった位置。
			float64 Cursor = StartU;
			for ( int32 Section = 0; Section < Sections; ++Section )
			{
				// この小さな区間の、整数の繰り返し番号。
				const float64 Repeat = FirstRepeat + Section;
				// この小さな区間の終わり（最後だけ、元の区間の終わりに合わせる）。
				const float64 Next = Section + 1 == Sections ? EndU : Repeat + 1.0;
				// 元の区間の中で、この小さな区間の始まりになる割合。
				const float64 FirstTime = EndU > StartU ? ( Cursor - StartU ) / ( EndU - StartU ) : 0.0;
				// 元の区間の中で、この小さな区間の終わりになる割合。
				const float64 LastTime = EndU > StartU ? ( Next - StartU ) / ( EndU - StartU ) : 1.0;
				// 新しい 4 つの頂点を足す先頭の番号。
				const uint16 Base = static_cast<uint16>( SplitVertices.Num() );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2], Mesh.Vertices[( Index + 1 ) * 2], FirstTime, static_cast<float32>( Cursor - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2 + 1], Mesh.Vertices[( Index + 1 ) * 2 + 1], FirstTime, static_cast<float32>( Cursor - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2], Mesh.Vertices[( Index + 1 ) * 2], LastTime, static_cast<float32>( Next - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2 + 1], Mesh.Vertices[( Index + 1 ) * 2 + 1], LastTime, static_cast<float32>( Next - Repeat ) ) );
				SplitIndices.Add( Base );
				SplitIndices.Add( static_cast<uint16>( Base + 1 ) );
				SplitIndices.Add( static_cast<uint16>( Base + 2 ) );
				SplitIndices.Add( static_cast<uint16>( Base + 1 ) );
				SplitIndices.Add( static_cast<uint16>( Base + 3 ) );
				SplitIndices.Add( static_cast<uint16>( Base + 2 ) );
				Cursor = Next;
			}
		}
		Mesh.Vertices = MoveTemp( SplitVertices );
		Mesh.Indices = MoveTemp( SplitIndices );
		return true;
	}

	// 帯の展開の本体。失敗のときは false（呼び出し側が、Mesh を空へ戻す）。
	bool BuildRibbon( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, const FDrawRibbonCamera& Camera, bool bSplitTexture, FDrawRibbonMesh& Mesh )
	{
		// 点の数・画像の繰り返し・端の補助点の指定は、命令の約束と同じ範囲に限る。
		if ( Points == nullptr || Data.NumPoints < 2 || Data.NumPoints > FDrawRibbonData::MaxPoints || Data.Texture < -1 || !NMath::IsFinite( Data.UVRepeat ) || Data.UVRepeat <= 0.0f || Data.JoinFlags < 0
			|| ( Data.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0
			|| Data.NumPoints - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 )
		{
			return false;
		}
		// 隣り合う点の間の向き（重なった点の区間は 0 のまま）。
		TArray<FVector3> Segments;
		Segments.SetNum( Data.NumPoints - 1 );
		// 経路全体に、描ける長さがあるか。
		bool bHasSegment = false;
		for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
		{
			// 向きと頂点の作成に使う点。
			const FDrawRibbonData::FPoint& Point = Points[Index];
			if ( !NMath::IsFinite( Point.Pos[0] ) || !NMath::IsFinite( Point.Pos[1] ) || !NMath::IsFinite( Point.Pos[2] ) || !NMath::IsFinite( Point.Width ) || Point.Width < 0.0f || !NMath::IsFinite( Point.U ) || Point.U < 0.0f
				|| !NMath::IsFinite( Point.U * Data.UVRepeat ) )
			{
				return false;
			}
			if ( Index > 0 )
			{
				if ( Point.U < Points[Index - 1].U )
				{
					return false;
				}
				Segments[Index - 1] = RibbonDirection( FVector3( Points[Index - 1].Pos[0], Points[Index - 1].Pos[1], Points[Index - 1].Pos[2] ), FVector3( Point.Pos[0], Point.Pos[1], Point.Pos[2] ), FVector3( 0.0f, 0.0f, 0.0f ) );
				bHasSegment = bHasSegment || Segments[Index - 1].LengthSquared() > 0.5f;
			}
		}
		if ( !bHasSegment )
		{
			return false;
		}
		// 各点へ入ってくる、最後の長さのある向き。
		TArray<FVector3> Incoming;
		Incoming.SetNum( Data.NumPoints );
		// 各点から出ていく、最後の長さのある向き。
		TArray<FVector3> Outgoing;
		Outgoing.SetNum( Data.NumPoints );
		// 重なった点の並びを越えて引き継ぐ向き。
		FVector3 Last = FVector3( 0.0f, 0.0f, 0.0f );
		for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
		{
			if ( Index > 0 && Segments[Index - 1].LengthSquared() > 0.5f )
			{
				Last = Segments[Index - 1];
			}
			Incoming[Index] = Last;
		}
		Last = FVector3( 0.0f, 0.0f, 0.0f );
		for ( int32 Index = Data.NumPoints - 1; Index >= 0; --Index )
		{
			if ( Index + 1 < Data.NumPoints && Segments[Index].LengthSquared() > 0.5f )
			{
				Last = Segments[Index];
			}
			Outgoing[Index] = Last;
		}
		// カメラの位置と向きと、カメラの位置に重なる点で使う代わりの視線。
		const FVector3 CameraBack = Camera.Front * -1.0f;
		// 各点の両側に作る、基本の頂点列。
		Mesh.Vertices.SetNum( Data.NumPoints * 2 );
		// 隣り合う両側の頂点を結ぶ、三角形の番号。
		Mesh.Indices.Reserve( ( Data.NumPoints - 1 ) * 6 );
		for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
		{
			// 幅・色・累積距離を持つ点。
			const FDrawRibbonData::FPoint& Point = Points[Index];
			// 点が指定する、帯の中心。
			const FVector3 Position = FVector3( Point.Pos[0], Point.Pos[1], Point.Pos[2] );
			// この点からカメラへ向かう単位ベクトル。
			const FVector3 ToCamera = RibbonDirection( Position, Camera.Position, CameraBack );
			// 端や重なった点で、代わりに使う前の向き。
			const FVector3 Before = Incoming[Index].LengthSquared() > 0.5f ? Incoming[Index] : Outgoing[Index];
			// 端や重なった点で、代わりに使う後ろの向き。
			const FVector3 After = Outgoing[Index].LengthSquared() > 0.5f ? Outgoing[Index] : Before;
			// 前の区間に沿った幅の方向。
			const FVector3 BeforeSide = RibbonSide( Before, ToCamera, Camera.Right, Camera.Up );
			// 後ろの区間に沿った幅の方向。
			const FVector3 AfterSide = RibbonSide( After, ToCamera, Camera.Right, Camera.Up );
			// 両側の向きを平均した、角（miter）の方向。
			const FVector3 Miter = RibbonDirection( FVector3( 0.0f, 0.0f, 0.0f ), BeforeSide + AfterSide, AfterSide );
			// 鋭い角で幅が伸びすぎないための補正の率。
			const float32 MiterScale = 1.0f / NMath::Max( NMath::Abs( FVector3::Dot( Miter, AfterSide ) ), 0.5f );
			// 中心から両側へずらす、半分の幅のベクトル。
			const FVector3 Offset = Miter * ( Point.Width * ( 0.5f * MiterScale ) );
			// 画像を繰り返す前の、連続した横の座標。
			const float32 U = Point.U * Data.UVRepeat;
			for ( int32 Side = 0; Side < 2; ++Side )
			{
				// この点の左右どちらかにある頂点。
				FDrawVertex3D& Vertex = Mesh.Vertices[Index * 2 + Side];
				Vertex = FDrawVertex3D();
				const FVector3 VertexPosition = Side == 0 ? Position + Offset : Position - Offset;
				if ( !NMath::IsFinite( VertexPosition.x ) || !NMath::IsFinite( VertexPosition.y ) || !NMath::IsFinite( VertexPosition.z ) )
				{
					return false;
				}
				Vertex.Position[0] = VertexPosition.x;
				Vertex.Position[1] = VertexPosition.y;
				Vertex.Position[2] = VertexPosition.z;
				Vertex.Normal[0] = ToCamera.x;
				Vertex.Normal[1] = ToCamera.y;
				Vertex.Normal[2] = ToCamera.z;
				// 下位のバイトから、赤・緑・青・不透明度。
				Vertex.Diffuse[0] = static_cast<uint8>( Point.Color & 0xFFu );
				Vertex.Diffuse[1] = static_cast<uint8>( ( Point.Color >> 8 ) & 0xFFu );
				Vertex.Diffuse[2] = static_cast<uint8>( ( Point.Color >> 16 ) & 0xFFu );
				Vertex.Diffuse[3] = static_cast<uint8>( ( Point.Color >> 24 ) & 0xFFu );
				Vertex.Specular[0] = 0;
				Vertex.Specular[1] = 0;
				Vertex.Specular[2] = 0;
				Vertex.Specular[3] = 0;
				Vertex.U = U;
				Vertex.V = static_cast<float32>( Side );
			}
			if ( Index + 1 < Data.NumPoints && !( Index == 0 && ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 2 == Data.NumPoints && ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) )
			{
				// 区間の先頭側にある 2 つの頂点の番号。
				const uint16 First = static_cast<uint16>( Index * 2 );
				Mesh.Indices.Add( First );
				Mesh.Indices.Add( static_cast<uint16>( First + 1 ) );
				Mesh.Indices.Add( static_cast<uint16>( First + 2 ) );
				Mesh.Indices.Add( static_cast<uint16>( First + 1 ) );
				Mesh.Indices.Add( static_cast<uint16>( First + 3 ) );
				Mesh.Indices.Add( static_cast<uint16>( First + 2 ) );
			}
		}
		// 画像の繰り返しで分けるのは、必要なときだけ（材質の描画は、連続した U のまま使う）。
		return !( bSplitTexture && Data.Texture >= 0 ) || SplitRibbonTexture( Data, Mesh );
	}
}

bool NDrawRibbonMesh::Build( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, const FDrawRibbonCamera& Camera, bool bSplitTexture, FDrawRibbonMesh& Mesh )
{
	// 呼び出しのたびに空から始め、失敗のときも空へ戻す（成功のときだけ、今回の結果が残る）。
	Mesh.Vertices.Reset();
	Mesh.Indices.Reset();
	if ( !BuildRibbon( Data, Points, Camera, bSplitTexture, Mesh ) )
	{
		Mesh.Vertices.Reset();
		Mesh.Indices.Reset();
		return false;
	}
	return true;
}
