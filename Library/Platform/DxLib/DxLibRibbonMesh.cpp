// SPDX-License-Identifier: NOASSERTION
#include "DxLibRibbonMesh.h"

#include <algorithm>
#include <cmath>

namespace
{
	// 大きな座標差も二倍精度で求め、同じ位置なら指定の向きを返す。
	VECTOR RibbonDirection( const VECTOR& From, const VECTOR& To, const VECTOR& Fallback )
	{
		// 終点と始点の差をfloatの桁あふれ前に広げるX成分。
		const float64 X = static_cast<float64>( To.x ) - From.x;
		// 終点と始点の差をfloatの桁あふれ前に広げるY成分。
		const float64 Y = static_cast<float64>( To.y ) - From.y;
		// 終点と始点の差をfloatの桁あふれ前に広げるZ成分。
		const float64 Z = static_cast<float64>( To.z ) - From.z;
		// 安全に単位化するための三次元距離。
		const float64 Length = std::sqrt( X * X + Y * Y + Z * Z );
		if ( !std::isfinite( Length ) || Length <= 1.0e-8 ) return Fallback;
		return VGet( static_cast<float32>( X / Length ), static_cast<float32>( Y / Length ), static_cast<float32>( Z / Length ) );
	}

	// 経路と視線に垂直な幅方向を求め、平行な場合はカメラ軸から代替する。
	VECTOR RibbonSide( const VECTOR& Tangent, const VECTOR& ToCamera, const VECTOR& CameraRight, const VECTOR& CameraUp )
	{
		// 経路と視線へ垂直に交わる通常の幅方向。
		VECTOR Side = VCross( Tangent, ToCamera );
		if ( VSquareSize( Side ) < 1.0e-10f ) Side = VSub( CameraRight, VScale( Tangent, VDot( CameraRight, Tangent ) ) );
		if ( VSquareSize( Side ) < 1.0e-10f ) Side = VSub( CameraUp, VScale( Tangent, VDot( CameraUp, Tangent ) ) );
		return RibbonDirection( VGet( 0.0f, 0.0f, 0.0f ), Side, CameraRight );
	}

	// 旧描画の画像境界へ追加する頂点を補間する。
	VERTEX3D RibbonVertexBetween( const VERTEX3D& First, const VERTEX3D& Last, float64 Time, float32 U )
	{
		// 位置などの連続値を桁あふれしにくい精度で補間する関数。
		const auto Mix = [&]( float32 A, float32 B ) { return static_cast<float32>( static_cast<float64>( A ) + ( static_cast<float64>( B ) - A ) * Time ); };
		// 色成分を丸めながらbyte範囲へ収める関数。
		const auto MixByte = [&]( uint8 A, uint8 B ) { return static_cast<uint8>( std::clamp( Mix( static_cast<float32>( A ), static_cast<float32>( B ) ), 0.0f, 255.0f ) + 0.5f ); };
		// 先頭頂点を基準にした、切れ目へ挿入する結果。
		VERTEX3D Result = First;
		Result.pos = VGet( Mix( First.pos.x, Last.pos.x ), Mix( First.pos.y, Last.pos.y ), Mix( First.pos.z, Last.pos.z ) );
		Result.norm = RibbonDirection( VGet( 0.0f, 0.0f, 0.0f ), VGet( Mix( First.norm.x, Last.norm.x ), Mix( First.norm.y, Last.norm.y ), Mix( First.norm.z, Last.norm.z ) ), First.norm );
		Result.dif = GetColorU8( MixByte( First.dif.r, Last.dif.r ), MixByte( First.dif.g, Last.dif.g ), MixByte( First.dif.b, Last.dif.b ), MixByte( First.dif.a, Last.dif.a ) );
		Result.u = U;
		return Result;
	}

	// 旧Ribbonの画像反復を整数U境界で分割する。
	bool SplitRibbonTexture( const FDrawRibbonData& Data, FDxLibRibbonMesh& Mesh )
	{
		// 分割後の帯の頂点。1区間につき4個なので最大32768個。
		TArray<VERTEX3D> SplitVertices;
		// 分割後の帯の三角形番号。
		TArray<unsigned short> SplitIndices;
		// 今回作った画像反復区間の数。
		int32 TotalSections = 0;
		for ( int32 Index = 0; Index + 1 < Data.NumPoints; ++Index )
		{
			if ( ( Index == 0 && ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) || ( Index + 2 == Data.NumPoints && ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) ) continue;
			// 現区間が画像座標上で始まる位置。
			const float64 StartU = Mesh.Vertices[Index * 2].u;
			// 現区間が画像座標上で終わる位置。
			const float64 EndU = Mesh.Vertices[( Index + 1 ) * 2].u;
			// 現区間の最初の整数反復番号。
			const float64 FirstRepeat = std::floor( StartU );
			// 現区間を整数境界で分けるときに必要な区間数。
			const float64 Required = std::max( 1.0, std::ceil( EndU ) - FirstRepeat );
			if ( !std::isfinite( Required ) || Required > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TotalSections ) ) return false;
			// 上限確認後に整数化した現区間の反復数。
			const int32 Sections = static_cast<int32>( Required );
			TotalSections += Sections;
			// 一つ前の小区間が終わった長手位置。
			float64 Cursor = StartU;
			for ( int32 Section = 0; Section < Sections; ++Section )
			{
				// 今回の小区間が使う整数反復番号。
				const float64 Repeat = FirstRepeat + Section;
				// 小区間の終端。最後だけ元の区間終端に合わせる。
				const float64 Next = Section + 1 == Sections ? EndU : Repeat + 1.0;
				// 元の区間内で小区間始端となる補間割合。
				const float64 FirstTime = EndU > StartU ? ( Cursor - StartU ) / ( EndU - StartU ) : 0.0;
				// 元の区間内で小区間終端となる補間割合。
				const float64 LastTime = EndU > StartU ? ( Next - StartU ) / ( EndU - StartU ) : 1.0;
				// 新しい四頂点を追加する先頭番号。
				const unsigned short Base = static_cast<unsigned short>( SplitVertices.Num() );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2], Mesh.Vertices[( Index + 1 ) * 2], FirstTime, static_cast<float32>( Cursor - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2 + 1], Mesh.Vertices[( Index + 1 ) * 2 + 1], FirstTime, static_cast<float32>( Cursor - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2], Mesh.Vertices[( Index + 1 ) * 2], LastTime, static_cast<float32>( Next - Repeat ) ) );
				SplitVertices.Add( RibbonVertexBetween( Mesh.Vertices[Index * 2 + 1], Mesh.Vertices[( Index + 1 ) * 2 + 1], LastTime, static_cast<float32>( Next - Repeat ) ) );
				SplitIndices.Add( Base );
				SplitIndices.Add( static_cast<unsigned short>( Base + 1 ) );
				SplitIndices.Add( static_cast<unsigned short>( Base + 2 ) );
				SplitIndices.Add( static_cast<unsigned short>( Base + 1 ) );
				SplitIndices.Add( static_cast<unsigned short>( Base + 3 ) );
				SplitIndices.Add( static_cast<unsigned short>( Base + 2 ) );
				Cursor = Next;
			}
		}
		Mesh.Vertices = std::move( SplitVertices );
		Mesh.Indices = std::move( SplitIndices );
		return true;
	}
}

bool NDxLibRibbonMesh::Build( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, bool bSplitTexture, FDxLibRibbonMesh& Mesh )
{
	// 呼び出しごとに出力を空へ戻し、成功時だけ今回の結果を有効にする。
	Mesh.Vertices.Reset();
	Mesh.Indices.Reset();
	// 点数・画像反復・ghost flagは呼び出し元契約と同じ範囲に限る。
	if ( Points == nullptr || Data.NumPoints < 2 || Data.NumPoints > FDrawRibbonData::MaxPoints || Data.Texture < -1 || !std::isfinite( Data.UVRepeat ) || Data.UVRepeat <= 0.0f || Data.JoinFlags < 0 || ( Data.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || Data.NumPoints - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 ) return false;
	// 隣接する経路点間の接線方向。重複点の区間は0のまま。
	TArray<VECTOR> Segments;
	Segments.SetNum( Data.NumPoints - 1 );
	// 経路全体に描画可能な長さがあるか。
	bool bHasSegment = false;
	for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
	{
		// 今回の接線と頂点作成に使う経路点。
		const FDrawRibbonData::FPoint& Point = Points[Index];
		if ( !std::isfinite( Point.Pos[0] ) || !std::isfinite( Point.Pos[1] ) || !std::isfinite( Point.Pos[2] ) || !std::isfinite( Point.Width ) || Point.Width < 0.0f || !std::isfinite( Point.U ) || Point.U < 0.0f || !std::isfinite( Point.U * Data.UVRepeat ) ) return false;
		if ( Index > 0 )
		{
			if ( Point.U < Points[Index - 1].U ) return false;
			Segments[Index - 1] = RibbonDirection( VGet( Points[Index - 1].Pos[0], Points[Index - 1].Pos[1], Points[Index - 1].Pos[2] ), VGet( Point.Pos[0], Point.Pos[1], Point.Pos[2] ), VGet( 0.0f, 0.0f, 0.0f ) );
			bHasSegment = bHasSegment || VSquareSize( Segments[Index - 1] ) > 0.5f;
		}
	}
	if ( !bHasSegment ) return false;
	// 各点へ入る最後の長さのある接線。
	TArray<VECTOR> Incoming;
	Incoming.SetNum( Data.NumPoints );
	// 各点から出る最後の長さのある接線。
	TArray<VECTOR> Outgoing;
	Outgoing.SetNum( Data.NumPoints );
	// 重複点の並びを越えて引き継ぐ接線。
	VECTOR Last = VGet( 0.0f, 0.0f, 0.0f );
	for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
	{
		if ( Index > 0 && VSquareSize( Segments[Index - 1] ) > 0.5f ) Last = Segments[Index - 1];
		Incoming[Index] = Last;
	}
	Last = VGet( 0.0f, 0.0f, 0.0f );
	for ( int32 Index = Data.NumPoints - 1; Index >= 0; --Index )
	{
		if ( Index + 1 < Data.NumPoints && VSquareSize( Segments[Index] ) > 0.5f ) Last = Segments[Index];
		Outgoing[Index] = Last;
	}
	// 帯の幅方向を決めるカメラ位置。
	const VECTOR CameraPosition = GetCameraPosition();
	// 視線平行時に幅方向へ使うカメラ右軸。
	const VECTOR CameraRight = GetCameraRightVector();
	// 右軸とも平行なときに使うカメラ上軸。
	const VECTOR CameraUp = GetCameraUpVector();
	// カメラ位置と重なる経路点の代替視線。
	const VECTOR CameraBack = VScale( GetCameraFrontVector(), -1.0f );
	// 各経路点の両側に作る基本頂点列。
	Mesh.Vertices.SetNum( Data.NumPoints * 2 );
	// 隣接する両側頂点を結ぶ三角形番号。
	Mesh.Indices.Reserve( ( Data.NumPoints - 1 ) * 6 );
	for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
	{
		// 現在の幅・色・累積距離を持つ経路点。
		const FDrawRibbonData::FPoint& Point = Points[Index];
		// 経路点が指定する帯の中心。
		const VECTOR Position = VGet( Point.Pos[0], Point.Pos[1], Point.Pos[2] );
		// この点からカメラへ向かう単位ベクトル。
		const VECTOR ToCamera = RibbonDirection( Position, CameraPosition, CameraBack );
		// 端点や重複点で代わりに使う前方接線。
		const VECTOR Before = VSquareSize( Incoming[Index] ) > 0.5f ? Incoming[Index] : Outgoing[Index];
		// 端点や重複点で代わりに使う後方接線。
		const VECTOR After = VSquareSize( Outgoing[Index] ) > 0.5f ? Outgoing[Index] : Before;
		// 前区間に沿った幅方向。
		const VECTOR BeforeSide = RibbonSide( Before, ToCamera, CameraRight, CameraUp );
		// 後区間に沿った幅方向。
		const VECTOR AfterSide = RibbonSide( After, ToCamera, CameraRight, CameraUp );
		// 両側の向きを平均したmiter方向。
		const VECTOR Miter = RibbonDirection( VGet( 0.0f, 0.0f, 0.0f ), VAdd( BeforeSide, AfterSide ), AfterSide );
		// 鋭い角で幅が伸びすぎないための補正率。
		const float32 MiterScale = 1.0f / std::max( std::fabs( VDot( Miter, AfterSide ) ), 0.5f );
		// 中心から両側へずらす半幅ベクトル。
		const VECTOR Offset = VScale( Miter, Point.Width * ( 0.5f * MiterScale ) );
		// 画像を繰り返す前の連続横座標。
		const float32 U = Point.U * Data.UVRepeat;
		// 下位byteからRGBAを復元した頂点色。
		const COLOR_U8 Color = GetColorU8( static_cast<int32>( Point.Color & 0xFFu ), static_cast<int32>( ( Point.Color >> 8 ) & 0xFFu ), static_cast<int32>( ( Point.Color >> 16 ) & 0xFFu ), static_cast<int32>( ( Point.Color >> 24 ) & 0xFFu ) );
		for ( int32 Side = 0; Side < 2; ++Side )
		{
			// 今回の点の左右どちらかにある頂点。
			VERTEX3D& Vertex = Mesh.Vertices[Index * 2 + Side];
			Vertex = {};
			Vertex.pos = Side == 0 ? VAdd( Position, Offset ) : VSub( Position, Offset );
			if ( !std::isfinite( Vertex.pos.x ) || !std::isfinite( Vertex.pos.y ) || !std::isfinite( Vertex.pos.z ) ) return false;
			Vertex.norm = ToCamera;
			Vertex.dif = Color;
			Vertex.spc = GetColorU8( 0, 0, 0, 0 );
			Vertex.u = U;
			Vertex.v = static_cast<float32>( Side );
		}
		if ( Index + 1 < Data.NumPoints && !( Index == 0 && ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 2 == Data.NumPoints && ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) )
		{
			// 区間の先頭側にある二頂点番号。
			const unsigned short First = static_cast<unsigned short>( Index * 2 );
			Mesh.Indices.Add( First );
			Mesh.Indices.Add( static_cast<unsigned short>( First + 1 ) );
			Mesh.Indices.Add( static_cast<unsigned short>( First + 2 ) );
			Mesh.Indices.Add( static_cast<unsigned short>( First + 1 ) );
			Mesh.Indices.Add( static_cast<unsigned short>( First + 3 ) );
			Mesh.Indices.Add( static_cast<unsigned short>( First + 2 ) );
		}
	}
	// 旧描画だけ画像反復ごとに分け、新材質描画は連続Uを保持する。
	if ( bSplitTexture && Data.Texture >= 0 && !SplitRibbonTexture( Data, Mesh ) ) return false;
	return true;
}
