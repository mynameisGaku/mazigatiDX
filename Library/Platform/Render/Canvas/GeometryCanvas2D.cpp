// SPDX-License-Identifier: MIT
#include "GeometryCanvas2D.h"
#include "Library/Math/MathUtils.h"
#include "Library/Core/Types/Numeric.h"

CGeometryCanvas2D::~CGeometryCanvas2D() { EndFrame(); }

bool CGeometryCanvas2D::BeginFrame( IRenderGeometry3D &InGeometry, IRenderTextureResources &InTextures, IRenderTextRasterizer &InText, FRenderClipRect &InClip, float32 InScale )
{
	if ( Geometry || InGeometry.GetWidth() <= 0 || InGeometry.GetHeight() <= 0 || !NMath::IsFinite( InScale ) || InScale <= 0 || InScale > 16384 )
	{
		return false;
	}
	Geometry = &InGeometry;
	Textures = &InTextures;
	TextRasterizer = &InText;
	Clip = &InClip;
	Width = Geometry->GetWidth();
	Height = Geometry->GetHeight();
	Scale = InScale;
	*Clip = { 0, 0, Width, Height };
	ClipStack.Reset();
	bFailed = false;
	bLinear = false;
	bAntiAlias = true;
	return true;
}

void CGeometryCanvas2D::EndFrame()
{
	if ( Textures )
	{
		for ( FTextureResourceHandle Image : TextImages )
		{
			Textures->ReleaseTexture( Image );
		}
	}
	TextImages.Reset();
	TextBytes = 0;
	ClipStack.Reset();
	Geometry = nullptr;
	Textures = nullptr;
	TextRasterizer = nullptr;
	Clip = nullptr;
}

FGeometryVertex CGeometryCanvas2D::Vertex( float32 X, float32 Y, FColor32 Color, float32 U, float32 V ) const
{
	return { { X * Scale * 2 / Width - 1, 1 - Y * Scale * 2 / Height, 0, 1 }, Color, U, V };
}

void CGeometryCanvas2D::Draw( const TArray<FGeometryVertex> &Vertices, const FTextureDrawView *Texture )
{
	if ( !Geometry || Vertices.Num() == 0 )
	{
		return;
	}
	FGeometryDrawState State; // 2Dは基本3Dの深度を読まず、書き換えない。
	State.bTestDepth = State.bWriteDepth = false;
	State.bAlphaBlend = true;
	State.bLinearFilter = bLinear;
	State.AddressU = State.AddressV = EGeometryTextureAddress::Clamp;
	if ( Texture )
	{
		bFailed = !Geometry->DrawTexturedGeometry( Vertices.GetData(), Vertices.Num(), State, *Texture ) || bFailed;
	}
	else
	{
		bFailed = !Geometry->DrawGeometry( Vertices.GetData(), Vertices.Num(), State ) || bFailed;
	}
}

void CGeometryCanvas2D::Polygon( const float32 *Points, int32 Count, FColor32 Color, bool bFill, float32 Thickness )
{
	if ( !Geometry || !Points || Count < 3 || Count > 512 || Color.a == 0 || !NMath::IsFinite( Thickness ) || ( !bFill && Thickness <= 0 ) )
	{
		return;
	}
	float32 Area = 0; // 画面上の向きから外側の法線を決める。
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const int32 Next = ( Index + 1 ) % Count; // 閉じた輪郭の次の点。
		if ( !NMath::IsFinite( Points[Index * 2] ) || !NMath::IsFinite( Points[Index * 2 + 1] ) || NMath::Abs( Points[Index * 2] ) > 1000000 || NMath::Abs( Points[Index * 2 + 1] ) > 1000000 )
		{
			return;
		}
		Area += Points[Index * 2] * Points[Next * 2 + 1] - Points[Next * 2] * Points[Index * 2 + 1];
	}
	if ( NMath::Abs( Area ) < 0.00001f )
	{
		return;
	}
	TArray<float32> Normals; // 隣接する辺で共有する頂点の外向き方向。
	Normals.SetNum( Count * 2 );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const int32 Previous = ( Index + Count - 1 ) % Count, Next = ( Index + 1 ) % Count; // 頂点へ入る辺と出る辺。
		float32 X0 = Points[Index * 2] - Points[Previous * 2], Y0 = Points[Index * 2 + 1] - Points[Previous * 2 + 1]; // 入る辺。
		float32 X1 = Points[Next * 2] - Points[Index * 2], Y1 = Points[Next * 2 + 1] - Points[Index * 2 + 1]; // 出る辺。
		const float32 Length0 = NMath::Sqrt( X0 * X0 + Y0 * Y0 ), Length1 = NMath::Sqrt( X1 * X1 + Y1 * Y1 ); // 方向の正規化用。
		if ( Length0 < 0.00001f || Length1 < 0.00001f )
		{
			return;
		}
		X0 /= Length0;
		Y0 /= Length0;
		X1 /= Length1;
		Y1 /= Length1;
		const float32 Factor = ( Area > 0 ? 1.0f : -1.0f ) / NMath::Max( 0.25f, 1 + X0 * X1 + Y0 * Y1 ); // 鋭角で過大な頂点を作らない。
		Normals[Index * 2] = ( Y0 + Y1 ) * Factor;
		Normals[Index * 2 + 1] = -( X0 + X1 ) * Factor;
	}
	TArray<FGeometryVertex> Vertices; // 同じ描画内の塗りと端をまとめる。
	const FColor32 Transparent( Color.r, Color.g, Color.b, 0 ); // 非乗算済みの透明色。
	const auto Point = [&]( int32 Index, float32 Offset, FColor32 Value ) // 外向き方向へ指定した幅だけ移動する。
	{
		return Vertex( Points[Index * 2] + Normals[Index * 2] * Offset, Points[Index * 2 + 1] + Normals[Index * 2 + 1] * Offset, Value );
	};
	const auto Band = [&]( float32 Inner, float32 Outer, FColor32 InnerColor, FColor32 OuterColor ) // 閉じた輪郭に隙間のない帯を作る。
	{
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			const int32 Next = ( Index + 1 ) % Count; // この辺を二枚の三角形で覆う。
			const FGeometryVertex A = Point( Index, Inner, InnerColor ), B = Point( Next, Inner, InnerColor ), C = Point( Next, Outer, OuterColor ), D = Point( Index, Outer, OuterColor ); // 共有する四隅。
			Vertices.Append( { A, B, C, A, C, D } );
		}
	};
	if ( bFill )
	{
		const float32 Inner = bAntiAlias ? -0.5f : 0; // 補間を付ける場合だけ中心側へ半画素寄せる。
		for ( int32 Index = 1; Index + 1 < Count; ++Index )
		{
			Vertices.Append( { Point( 0, Inner, Color ), Point( Index, Inner, Color ), Point( Index + 1, Inner, Color ) } );
		}
		if ( bAntiAlias )
		{
			Band( -0.5f, 0.5f, Color, Transparent );
		}
	}
	else
	{
		const float32 Inner = NMath::Max( 0.0f, Thickness * 0.5f - ( bAntiAlias ? 0.5f : 0 ) ); // 完全に覆う輪郭の半幅。
		Band( -Inner, Inner, Color, Color );
		if ( bAntiAlias )
		{
			Band( Inner, Thickness * 0.5f + 0.5f, Color, Transparent );
			Band( -Thickness * 0.5f - 0.5f, -Inner, Transparent, Color );
		}
	}
	Draw( Vertices );
}

void CGeometryCanvas2D::Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness )
{
	const float32 Points[] = { X0, Y0, X1, Y0, X1, Y1, X0, Y1 }; // 四辺を共有する凸な点列。
	Polygon( Points, 4, Color, bFill, Thickness );
}

void CGeometryCanvas2D::Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness )
{
	const float32 X = X1 - X0, Y = Y1 - Y0, Length = NMath::Sqrt( X * X + Y * Y ); // 線に直交する幅を求める。
	if ( !NMath::IsFinite( Length ) || Length <= 0 || !NMath::IsFinite( Thickness ) || Thickness <= 0 )
	{
		return;
	}
	const float32 NX = Y * Thickness * 0.5f / Length, NY = -X * Thickness * 0.5f / Length; // 輪郭の半幅。
	const float32 Points[] = { X0 + NX, Y0 + NY, X1 + NX, Y1 + NY, X1 - NX, Y1 - NY, X0 - NX, Y0 - NY }; // 太さを持つ線分。
	Polygon( Points, 4, Color, true, 1 );
}

void CGeometryCanvas2D::Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments )
{
	if ( !NMath::IsFinite( Radius ) || Radius <= 0 || Radius > 1000000 )
	{
		return;
	}
	const int32 Count = Segments > 0 ? NMath::Clamp( Segments, 3, 512 ) : NMath::Clamp( static_cast<int32>( NMath::Ceil( NMath::Sqrt( Radius ) * 6 ) ), 12, 512 ); // 半径に応じた既定の分割。
	TArray<float32> Points; // 輪郭は閉じる点を重複させない。
	Points.SetNum( Count * 2 );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const float32 Angle = Index * 6.28318530718f / Count; // 一周を等分する角度。
		Points[Index * 2] = X + Radius * NMath::Cos( Angle );
		Points[Index * 2 + 1] = Y + Radius * NMath::Sin( Angle );
	}
	Polygon( Points.GetData(), Count, Color, bFill, Thickness );
}

void CGeometryCanvas2D::Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness ) { Polygon( Points, 3, Color, bFill, Thickness ); }
void CGeometryCanvas2D::Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness ) { Polygon( Points, 4, Color, bFill, Thickness ); }

void CGeometryCanvas2D::Image( const FTextureDrawView &Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color )
{
	if ( !Geometry || !Texture.Binding || Texture.Width <= 0 || Texture.Height <= 0 )
	{
		return;
	}
	const FGeometryVertex A = Vertex( Points[0], Points[1], Color, UV[0], UV[1] ), B = Vertex( Points[2], Points[3], Color, UV[2], UV[1] ), C = Vertex( Points[4], Points[5], Color, UV[2], UV[3] ), D = Vertex( Points[6], Points[7], Color, UV[0], UV[3] ); // 色とUVを持つ四隅。
	const TArray<FGeometryVertex> Vertices = { A, B, C, A, C, D }; // 凸四角形を二枚に分ける。
	Draw( Vertices, &Texture );
}

void CGeometryCanvas2D::Text( float32 X, float32 Y, FColor32 Color, const char *Begin, const char *End )
{
	if ( !Geometry || !Begin || Begin == End || Begin[0] == 0 || Color.a == 0 || !Clip || Clip->Left >= Clip->Right || Clip->Top >= Clip->Bottom )
	{
		return;
	}
	if ( TextImages.Num() >= 128 )
	{
		bFailed = true;
		return;
	}
	int32 Length = 0; // 最大64KiBまでの指定範囲だけを読む。
	if ( End )
	{
		if ( End < Begin || End - Begin > 65536 )
		{
			return;
		}
		Length = static_cast<int32>( End - Begin );
	}
	else
	{
		while ( Length < 65536 && Begin[Length] )
		{
			++Length;
		}
		if ( Length == 65536 )
		{
			return;
		}
	}
	TArray<FColor32> Pixels; // GPUを知らない文字実装が作る画像。
	int32 TextWidth = 0, TextHeight = 0; // 論理画素の文字寸法。
	if ( Length <= 0 )
	{
		return;
	}
	if ( !TextRasterizer->RasterizeText( TArrayView<const char>( Begin, Length ), Pixels, TextWidth, TextHeight ) || TextWidth <= 0 || TextHeight <= 0 || static_cast<int64>( TextWidth ) * TextHeight != Pixels.Num() )
	{
		bFailed = true;
		return;
	}
	if ( TextBytes + static_cast<uint64>( Pixels.Num() ) * sizeof( FColor32 ) > 32 * 1024 * 1024 )
	{
		bFailed = true;
		return;
	}
	const FTextureResourceHandle Handle = Textures->CreateTextureRGBA8( TextWidth, TextHeight, Pixels.GetData() ); // 所有は既存の画像サービスへ渡す。
	FTextureDrawView View; // この描画の間だけ借りるGPUの番号。
	if ( !Handle.IsValid() )
	{
		bFailed = true;
		return;
	}
	if ( !Textures->GetTextureResourceView( Handle, View ) )
	{
		Textures->ReleaseTexture( Handle );
		bFailed = true;
		return;
	}
	TextImages.Add( Handle );
	TextBytes += static_cast<uint64>( Pixels.Num() ) * sizeof( FColor32 );
	const float32 Points[8] = { X, Y, X + TextWidth, Y, X + TextWidth, Y + TextHeight, X, Y + TextHeight }, UV[4] = { 0, 0, 1, 1 }; // 文字も通常の画像と同じ切り取りと倍率で描く。
	const bool bPreviousFilter = bLinear; // 文字の補間を後続の画像へ持ち越さない。
	bLinear = true;
	Image( View, Points, UV, Color );
	bLinear = bPreviousFilter;
}

void CGeometryCanvas2D::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	if ( !Clip )
	{
		return;
	}
	ClipStack.Add( *Clip );
	const auto Coordinate = [&]( int32 Value, int32 Limit ) // 変換前に浮動小数点で範囲へ収め、整数の変換超過を避ける。
	{
		return static_cast<int32>( NMath::Clamp( Value * Scale, 0.0f, static_cast<float32>( Limit ) ) );
	};
	*Clip = { NMath::Max( Clip->Left, Coordinate( X0, Width ) ), NMath::Max( Clip->Top, Coordinate( Y0, Height ) ), NMath::Min( Clip->Right, Coordinate( X1, Width ) ), NMath::Min( Clip->Bottom, Coordinate( Y1, Height ) ) };
}

void CGeometryCanvas2D::PopClip()
{
	if ( Clip && ClipStack.Num() > 0 )
	{
		*Clip = ClipStack.Pop();
	}
}
bool CGeometryCanvas2D::SetTextureFilter( bool bInLinear )
{
	bLinear = bInLinear;
	return Geometry != nullptr;
}
void CGeometryCanvas2D::SetAntiAlias( bool bEnabled ) { bAntiAlias = bEnabled; }

bool CGeometryCanvas2D::DidFrameSucceed() const { return !bFailed; }
