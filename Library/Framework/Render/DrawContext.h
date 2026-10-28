// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_RENDER_DRAWCONTEXT_H
#define FRAMEWORK_RENDER_DRAWCONTEXT_H


#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawRibbon.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FDrawBuffer
//   描画コマンドの入れ物（4 バイト単位の並び。形式は DrawCommand.h を参照）。
//=============================================================================
struct FDrawBuffer
{
	TArray<uint32> Words;

	// 中身を空にする（確保した領域は残す。毎フレーム使い回すため）
	void Clear() { Words.Reset(); }
};

//=============================================================================
// FDrawContext
//   描画システムの中で、「何を描くか」を記録する窓口。DxLib は呼ばない（呼べない）ので、ワーカーのスレッドからも使える。
//   関数の名前と引数の順番は DxLib の DrawBox / DrawCircle / DrawLine / DrawString / DrawRotaGraph / DrawSphere3D などに合わせてある。
//
//   2D:
//   Scene.AddDrawSystem( "Marker.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FMarker& Marker )
//   {
//       const FVector3 Position = Transform.GetWorldPosition();
//       Draw.Box( X - Marker.HalfSize, Y - Marker.HalfSize, X + Marker.HalfSize, Y + Marker.HalfSize, Marker.Color );
//   } );
//
//   3D（ワールド座標。視錐台カリングや LOD の選択、行列の合成は、ここ（並列）で済ませてから記録するのが得）:
//   Scene.AddDrawSystem( "Mesh.Draw", [&Camera]( FDrawContext& Draw, const FTransformView& Transform, const FMeshInstance& Mesh )
//   {
//       if ( !Camera.Sees( Transform.GetWorldPosition(), Mesh.BoundingRadius ) ) return;   // カメラは読むだけ
//       Draw.Model( Mesh.ModelHandle, Transform.GetWorldMatrix(), Mesh.AnimAttachIndex, Mesh.AnimTime );
//   } );
//
//   - 記録した順番に実行される（1 つのオブジェクトの中では、記録した順。重なりもその通りになる）
//   - 色は FColor32（DxLib の GetColor への変換は、実行するとき、メインスレッドで行う）
//   - 描画の状態（Blend / CustomBlend / Tint / AddColor / AlphaTest / Filter / PushClip / Transform2D）と 3D 図形の変換（WorldMatrix）と描画先（PushTarget）は、そのオブジェクトの分が終わると元に戻る
//   - カメラ・ライト・Zバッファ・描画先は記録しない（呼び出し側が決めたものを使う）
//   - グラフィック / モデルのハンドルは、あらかじめ読み込んでおく（読み込みは DxLib の呼び出しなのでメインスレッドで）
//=============================================================================
// いまの合成方法をまとめたもの。ほかの合成方法で少し描いてから元に戻すときに、FDrawContext::GetBlendState で保存し、SetBlendState で戻す
// （Blend だけでは、CustomBlend で決めた合成方法を戻せない）
struct FBlendState
{
	// 合成の種類（カスタムブレンドのあとは Custom）
	EDrawBlend Mode = EDrawBlend::Opaque;
	// Alpha などの強さ（Mode が Custom のときは、Custom.Param と同じ）
	int32 Param = 255;
	// Mode が Custom のときの設定
	FCustomBlend Custom;
};

class FDrawContext
{
public:
	explicit FDrawContext( FDrawBuffer& InBuffer ) : Buffer( &InBuffer ) {}

	// これまでに記録したワード数（システムの実行機構が、オブジェクトごとの範囲を決めるのに使う）
	FORCEINLINE int32 GetNumWords() const { return Buffer->Words.Num(); }

	//-------------------------------------------------------------------------
	// 2D（画面座標）
	//-------------------------------------------------------------------------
	// 四角形（DrawBox）。bFill が false なら枠線だけ（Thickness は枠の太さ）
	FORCEINLINE void Box( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, bool bFill = true, int32 Thickness = 1 )
	{
		FDrawBoxData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Box, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 円（DrawCircle）
	FORCEINLINE void Circle( int32 X, int32 Y, int32 Radius, const FColor32& Color, bool bFill = true, int32 Thickness = 1 )
	{
		FDrawCircleData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Radius = Radius;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Circle, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 線（DrawLine）
	FORCEINLINE void Line( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, int32 Thickness = 1 )
	{
		FDrawLineData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Line, 0, Data );
	}

	// 点（DrawPixel）
	FORCEINLINE void Pixel( int32 X, int32 Y, const FColor32& Color )
	{
		FDrawPixelData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Pixel, 0, Data );
	}

	// 三角形（DrawTriangle）。頂点は (X0, Y0) (X1, Y1) (X2, Y2)。bFill が false なら枠線だけ（太さは 1）
	FORCEINLINE void Triangle( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, const FColor32& Color, bool bFill = true )
	{
		PushTriangle( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( X2 ), static_cast<float32>( Y2 ), Color, bFill, 1.0f, 0 );
	}

	// 四角形（DrawQuadrangle）。頂点を (X0, Y0) -> (X1, Y1) -> (X2, Y2) -> (X3, Y3) の順にたどる（台形や平行四辺形など、傾いた四角形用。軸に平行な四角形は Box）
	FORCEINLINE void Quad( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 X3, int32 Y3, const FColor32& Color, bool bFill = true )
	{
		PushQuad( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( X2 ), static_cast<float32>( Y2 ), static_cast<float32>( X3 ), static_cast<float32>( Y3 ), Color, bFill, 1.0f, 0 );
	}

	// 楕円（DrawOval）。(X, Y) が中心、Rx / Ry が横と縦の半径
	FORCEINLINE void Oval( int32 X, int32 Y, int32 Rx, int32 Ry, const FColor32& Color, bool bFill = true, int32 Thickness = 1 )
	{
		PushOval( static_cast<float32>( X ), static_cast<float32>( Y ), static_cast<float32>( Rx ), static_cast<float32>( Ry ), Color, bFill, static_cast<float32>( Thickness ), 0, 0 );
	}

	// 角の丸い四角形（DrawRoundRect）。Rx / Ry が角の丸みの横と縦の半径。bFill が false なら枠線だけ（太さは 1）
	FORCEINLINE void RoundBox( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Rx, int32 Ry, const FColor32& Color, bool bFill = true )
	{
		PushRoundBox( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( Rx ), static_cast<float32>( Ry ), Color, bFill, 1.0f, 0, 0 );
	}

	//-------------------------------------------------------------------------
	// なめらかな（アンチエイリアス付きの）2D 図形。座標は float32 で、ピクセルの途中にも置ける（ゆっくり動く物がガタつかない）。
	// 数値が有限でない（NaN・無限大）・大きすぎる（絶対値が 1e7 より大きい）・半径が負・太さが 0 以下のものは、記録しない。
	// NumSegments は円・楕円・角の丸みを何角形で近似するか。0 なら、半径から（誤差が 1/4 ピクセル以下になるように）決める
	//-------------------------------------------------------------------------
	// 線（DrawLineAA）
	FORCEINLINE void LineAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, float32 Thickness = 1.0f )
	{
		if ( !AllDrawable( X0, Y0, X1, Y1 ) || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawLineAAData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::LineAA, 0, Data );
	}

	// 四角形（DrawBoxAA）。bFill が false なら枠線だけ
	FORCEINLINE void BoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f )
	{
		if ( !AllDrawable( X0, Y0, X1, Y1 ) || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawBoxAAData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::BoxAA, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 円（DrawCircleAA）
	FORCEINLINE void CircleAA( float32 X, float32 Y, float32 Radius, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 )
	{
		if ( !AllDrawable( X, Y, Radius ) || Radius < 0.0f || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawCircleAAData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Radius = Radius;
		Data.Thickness = Thickness;
		Data.NumSegments = ClampSegments( NumSegments );
		Data.Color = Rgb( Color );
		Push( EDrawCommand::CircleAA, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 楕円（DrawOvalAA）。(X, Y) が中心、Rx / Ry が横と縦の半径
	FORCEINLINE void OvalAA( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 )
	{
		PushOval( X, Y, Rx, Ry, Color, bFill, Thickness, ClampSegments( NumSegments ), FDrawFlag::AntiAlias );
	}

	// 三角形（DrawTriangleAA）
	FORCEINLINE void TriangleAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f )
	{
		PushTriangle( X0, Y0, X1, Y1, X2, Y2, Color, bFill, Thickness, FDrawFlag::AntiAlias );
	}

	// 四角形（DrawQuadrangleAA）。頂点を順にたどる
	FORCEINLINE void QuadAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f )
	{
		PushQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, Color, bFill, Thickness, FDrawFlag::AntiAlias );
	}

	// 角の丸い四角形（DrawRoundRectAA）。Rx / Ry が角の丸みの横と縦の半径
	FORCEINLINE void RoundBoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill = true, float32 Thickness = 1.0f, int32 NumSegments = 0 )
	{
		PushRoundBox( X0, Y0, X1, Y1, Rx, Ry, Color, bFill, Thickness, ClampSegments( NumSegments ), FDrawFlag::AntiAlias );
	}

	// 文字列（DrawString）。文字列は記録のときにコピーされる（呼び出したあと、元の領域は使い回してよい）
	void Text( int32 X, int32 Y, const char* String, const FColor32& Color );
	// 書式付きの文字列（DrawFormatString）
	void TextF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... );

	// ワールド座標の帯を記録し、点列を複写する。補助点の区間は描かず、2..1024点以外や不正な数値は記録しない。
	// Widthは全幅、Uは累積距離。Texture=-1は色だけの帯。既存のWorldMatrixには影響されない。
	// 画像を整数U境界で分割した区間が8192を超える指定も、記録しない。
	void Ribbon3D( const FDrawRibbonData::FPoint* Points, int32 NumPoints, int32 Texture, float32 UVRepeat = 1.0f, int32 JoinFlags = 0 );

	// フォント指定の文字列（DrawStringToHandle）。FontHandle は CFont::GetHandle()。
	// Align で、X を文字列の左端 / 中央 / 右端にできる（幅の計算は実行のとき、メインスレッドで行う）。EdgeColor は縁取りのあるフォントのときの縁の色
	void TextWithFont( int32 FontHandle, int32 X, int32 Y, const char* String, const FColor32& Color, EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black );

	// 画像（DrawRotaGraphF）。(X, Y) が画像の中心。AngleRadians はラジアン。GraphHandle は LoadGraph などで得たもの
	FORCEINLINE void Sprite( float32 X, float32 Y, int32 GraphHandle, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		FDrawSpriteData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Scale = Scale;
		Data.Angle = AngleRadians;
		Data.Handle = GraphHandle;
		Push( EDrawCommand::Sprite, Flip( bFlipX, bFlipY ), Data );
	}

	// 画像の一部分（スプライトシートの 1 コマなど）を描く（DrawRectRotaGraph3F）。
	// 画像 GraphHandle の矩形 (SrcX, SrcY, SrcW, SrcH) のうち、矩形の中の点 (PivotX, PivotY)（矩形の左上が原点、ピクセル）が画面の (X, Y) に来る。
	// AngleRadians は Pivot まわりの回転、反転も Pivot を軸にする（反転しても Pivot は (X, Y) のまま）
	FORCEINLINE void SpriteRect( float32 X, float32 Y, int32 GraphHandle, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 PivotX, float32 PivotY,
								 float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		FDrawSpriteRectData Data;
		Data.X = X;
		Data.Y = Y;
		Data.ScaleX = ScaleX;
		Data.ScaleY = ScaleY;
		Data.Angle = AngleRadians;
		Data.PivotX = PivotX;
		Data.PivotY = PivotY;
		Data.Handle = GraphHandle;
		Data.SrcX = SrcX;
		Data.SrcY = SrcY;
		Data.SrcW = SrcW;
		Data.SrcH = SrcH;
		Push( EDrawCommand::SpriteRect, Flip( bFlipX, bFlipY ), Data );
	}

	// 画像（IImage を実装したもの。CTexture など）を渡す版の Sprite / SpriteRect。画像が nullptr・無効なら、何も記録しない
	template<typename TImage>
	FORCEINLINE void Sprite( float32 X, float32 Y, const TImage& Source, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		const FImageHandle Handle = ToHandle( Source );
		if ( Handle.IsValid() )
		{
			Sprite( X, Y, Handle.Value, Scale, AngleRadians, bFlipX, bFlipY );
		}
	}

	template<typename TImage>
	FORCEINLINE void SpriteRect( float32 X, float32 Y, const TImage& Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 PivotX, float32 PivotY,
								 float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		const FImageHandle Handle = ToHandle( Source );
		if ( Handle.IsValid() )
		{
			SpriteRect( X, Y, Handle.Value, SrcX, SrcY, SrcW, SrcH, PivotX, PivotY, ScaleX, ScaleY, AngleRadians, bFlipX, bFlipY );
		}
	}

	//-------------------------------------------------------------------------
	// 2D の画像の描画（引数の順番は、DxLib の DrawGraph / DrawExtendGraph / DrawModiGraph / DrawRectGraph / DrawRotaGraph3 / DrawTile / DrawCircleGauge などに合わせてある）
	//   画像は、IImage を実装したもの（CTexture など。TSharedPtr・ポインター・参照のどれでもよい）か、FImageHandle で渡す。IImage が nullptr・無効（IsValid が false）なら、何も記録しない。
	//   記録のあと、実行される（メインスレッドで描かれる）まで、画像を手放さないこと。座標は float32 のピクセル（左上が原点）。画像は透過して描く（画像のアルファ・透過色が効く）。
	//   描けない値（無効な画像・座標が NaN・無限大・絶対値 1e7 超・切り出す矩形の大きさが 0 以下・拡大率が 0 以下など）は、何も記録しない。
	//   ブレンドモード（Blend）と描画の色（Tint）は、そのまま効く。
	//-------------------------------------------------------------------------
	// 画像を等倍で描く（DrawGraphF）。(X, Y) が画像の左上。反転すると、画像の中だけが反転する（左上の位置は同じ）
	FORCEINLINE void Image( float32 X, float32 Y, FImageHandle Handle, bool bFlipX = false, bool bFlipY = false )
	{
		if ( !Handle.IsValid() || !AllDrawable( X, Y ) )
		{
			return;
		}
		FDrawImageData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::Image, Flip( bFlipX, bFlipY ), Data );
	}

	template<typename TImage>
	FORCEINLINE void Image( float32 X, float32 Y, const TImage& Source, bool bFlipX = false, bool bFlipY = false )
	{
		Image( X, Y, ToHandle( Source ), bFlipX, bFlipY );
	}

	// 画像を、矩形 (X0, Y0)-(X1, Y1) に合わせて拡大・縮小して描く（DrawExtendGraphF）。X0 > X1 や Y0 > Y1 なら、その向きに反転する
	FORCEINLINE void ImageStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, FImageHandle Handle )
	{
		if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1 ) )
		{
			return;
		}
		FDrawImageStretchData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::ImageStretch, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void ImageStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, const TImage& Source )
	{
		ImageStretch( X0, Y0, X1, Y1, ToHandle( Source ) );
	}

	// 画像を、4 つの角へ自由に変形して描く（DrawModiGraphF）。画像の 左上・右上・右下・左下 が、(X0, Y0) (X1, Y1) (X2, Y2) (X3, Y3) へ行く
	FORCEINLINE void ImageQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, FImageHandle Handle )
	{
		if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) )
		{
			return;
		}
		FDrawImageQuadData Data;
		Data.X[0] = X0;
		Data.Y[0] = Y0;
		Data.X[1] = X1;
		Data.Y[1] = Y1;
		Data.X[2] = X2;
		Data.Y[2] = Y2;
		Data.X[3] = X3;
		Data.Y[3] = Y3;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::ImageQuad, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void ImageQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const TImage& Source )
	{
		ImageQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, ToHandle( Source ) );
	}

	// 画像の一部分（矩形 (SrcX, SrcY, SrcW, SrcH)）を、等倍で描く（DrawRectGraphF）。(X, Y) が、切り出した部分の左上。スプライトシートの 1 コマなどに
	FORCEINLINE void ImagePart( float32 X, float32 Y, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle, bool bFlipX = false, bool bFlipY = false )
	{
		if ( !Handle.IsValid() || !AllDrawable( X, Y ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
		{
			return;
		}
		FDrawImagePartData Data;
		Data.X = X;
		Data.Y = Y;
		Data.SrcX = SrcX;
		Data.SrcY = SrcY;
		Data.SrcW = SrcW;
		Data.SrcH = SrcH;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::ImagePart, Flip( bFlipX, bFlipY ), Data );
	}

	template<typename TImage>
	FORCEINLINE void ImagePart( float32 X, float32 Y, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source, bool bFlipX = false, bool bFlipY = false )
	{
		ImagePart( X, Y, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ), bFlipX, bFlipY );
	}

	// 画像の一部分を、矩形 (X0, Y0)-(X1, Y1) に合わせて拡大・縮小して描く（DrawRectExtendGraphF）。X0 > X1 や Y0 > Y1 なら、その向きに反転する
	FORCEINLINE void ImagePartStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle )
	{
		if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1 ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
		{
			return;
		}
		FDrawImagePartStretchData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.SrcX = SrcX;
		Data.SrcY = SrcY;
		Data.SrcW = SrcW;
		Data.SrcH = SrcH;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::ImagePartStretch, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void ImagePartStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source )
	{
		ImagePartStretch( X0, Y0, X1, Y1, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ) );
	}

	// 画像の一部分を、4 つの角へ自由に変形して描く（DrawRectModiGraphF）。切り出した部分の 左上・右上・右下・左下 が、(X0, Y0) (X1, Y1) (X2, Y2) (X3, Y3) へ行く
	FORCEINLINE void ImagePartQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle )
	{
		if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
		{
			return;
		}
		FDrawImagePartQuadData Data;
		Data.X[0] = X0;
		Data.Y[0] = Y0;
		Data.X[1] = X1;
		Data.Y[1] = Y1;
		Data.X[2] = X2;
		Data.Y[2] = Y2;
		Data.X[3] = X3;
		Data.Y[3] = Y3;
		Data.SrcX = SrcX;
		Data.SrcY = SrcY;
		Data.SrcW = SrcW;
		Data.SrcH = SrcH;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::ImagePartQuad, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void ImagePartQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TImage& Source )
	{
		ImagePartQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, SrcX, SrcY, SrcW, SrcH, ToHandle( Source ) );
	}

	// 画像全体を、基準点（画像の中の点 (PivotX, PivotY)。左上が原点、ピクセル）が画面の (X, Y) に来るように、横・縦に別々の拡大率で拡大し、基準点まわりに回して描く（DrawRotaGraph3F）。
	// 反転しても、基準点は (X, Y) のまま。AngleRadians はラジアン。拡大率が負なら、その向きに反転する
	FORCEINLINE void SpritePivot( float32 X, float32 Y, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		if ( !Handle.IsValid() || !AllDrawable( X, Y, PivotX, PivotY, ScaleX, ScaleY, AngleRadians ) )
		{
			return;
		}
		FDrawSpritePivotData Data;
		Data.X = X;
		Data.Y = Y;
		Data.PivotX = PivotX;
		Data.PivotY = PivotY;
		Data.ScaleX = ScaleX;
		Data.ScaleY = ScaleY;
		Data.Angle = AngleRadians;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::SpritePivot, Flip( bFlipX, bFlipY ), Data );
	}

	template<typename TImage>
	FORCEINLINE void SpritePivot( float32 X, float32 Y, const TImage& Source, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		SpritePivot( X, Y, ToHandle( Source ), PivotX, PivotY, ScaleX, ScaleY, AngleRadians, bFlipX, bFlipY );
	}

	// 矩形 (X0, Y0)-(X1, Y1) を、画像の繰り返しで埋める（DrawTile）。座標は整数（DxLib の DrawTile と同じ）。
	// 画像の左上が、矩形の左上に合う（DxLib の DrawTile は画像の中心を矩形の中心に置くので、実行のときに補正する）。
	// OffsetX / OffsetY は並びの、右下向きのずれ（ピクセル。拡大率が 1 でないと、拡大率の整数倍に丸まる）、Scale は画像の拡大率（0 より大きい）、AngleRadians は画像の回転（ラジアン）
	FORCEINLINE void Tile( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Handle, int32 OffsetX = 0, int32 OffsetY = 0, float32 Scale = 1.0f, float32 AngleRadians = 0.0f )
	{
		if ( !Handle.IsValid() || Scale <= 0.0f || !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( OffsetX ), static_cast<float32>( OffsetY ), Scale, AngleRadians ) )
		{
			return;
		}
		FDrawTileData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.OffsetX = OffsetX;
		Data.OffsetY = OffsetY;
		Data.Scale = Scale;
		Data.Angle = AngleRadians;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::Tile, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void Tile( int32 X0, int32 Y0, int32 X1, int32 Y1, const TImage& Source, int32 OffsetX = 0, int32 OffsetY = 0, float32 Scale = 1.0f, float32 AngleRadians = 0.0f )
	{
		Tile( X0, Y0, X1, Y1, ToHandle( Source ), OffsetX, OffsetY, Scale, AngleRadians );
	}

	// 円グラフのように、画像を、真上から時計回りに Fraction（0..1。範囲の外は収める）の割合まで描く（DrawCircleGaugeF）。クールダウンの表示などに。
	// (X, Y) が画像の中心。StartFraction（0..1）は描き始める位置（真上が 0）で、そこから Fraction だけ描く（描き終わりが 1 を超えると、先頭へ回り込む。Fraction が 1 なら、描き始めの位置に関係なく全体）。
	// Scale は画像の拡大率（0 より大きい）。bFlipX / bFlipY は、グラフを左右・上下に反転する。DxLib のゲージは、画像の四角ではなく、画像の中心を中心とする円（半径は、画像の半分より少し大きい）を描く
	FORCEINLINE void CircleGauge( float32 X, float32 Y, float32 Fraction, FImageHandle Handle, float32 StartFraction = 0.0f, float32 Scale = 1.0f, bool bFlipX = false, bool bFlipY = false )
	{
		if ( !Handle.IsValid() || Scale <= 0.0f || !AllDrawable( X, Y, Fraction, StartFraction, Scale ) )
		{
			return;
		}
		FDrawCircleGaugeData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Percent = NMath::Clamp( Fraction, 0.0f, 1.0f ) * 100.0f;
		Data.StartPercent = NMath::Clamp( StartFraction, 0.0f, 1.0f ) * 100.0f;
		Data.Scale = Scale;
		Data.Handle = Handle.Value;
		Push( EDrawCommand::CircleGauge, Flip( bFlipX, bFlipY ), Data );
	}

	template<typename TImage>
	FORCEINLINE void CircleGauge( float32 X, float32 Y, float32 Fraction, const TImage& Source, float32 StartFraction = 0.0f, float32 Scale = 1.0f, bool bFlipX = false, bool bFlipY = false )
	{
		CircleGauge( X, Y, Fraction, ToHandle( Source ), StartFraction, Scale, bFlipX, bFlipY );
	}

	// 以降の描画に掛ける色（SetDrawBright）。白に戻すと元の色。そのオブジェクトの分が終わると白に戻る
	FORCEINLINE void Tint( const FColor32& Color )
	{
		FDrawBrightData Data;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Bright, 0, Data );
	}

	// 以降のブレンドモードを変える（SetDrawBlendMode）。Param は Alpha のときの不透明度（0..255。範囲の外は収める）など。
	// Custom と、EDrawBlend にない値は、何も記録しない（カスタムブレンドは CustomBlend で）。そのオブジェクトの分が終わると、ブレンドしない状態に戻る
	FORCEINLINE void Blend( EDrawBlend Mode, int32 Param = 255 )
	{
		if ( Mode == EDrawBlend::Custom || !IsInEnumRange( static_cast<int32>( Mode ), static_cast<int32>( EDrawBlend::PmaInvert ) ) )
		{
			return;
		}
		FDrawBlendData Data;
		Data.Mode = static_cast<int32>( Mode );
		Data.Param = NMath::Clamp( Param, 0, 255 );
		Push( EDrawCommand::Blend, 0, Data );
		CurrentBlendMode = Mode;
		CurrentBlendParam = Data.Param;
	}

	// カスタムブレンド（SetDrawCustomBlendMode）。Src（これから描く色）と Dest（描き先の色）に係数を掛けて、組み合わせ方で合わせる。
	// 例: 加算 = { SrcColor: SrcAlpha, DestColor: One, ColorOp: Add }。Blend で別のモードにするか、そのオブジェクトの分が終わると、元に戻る。
	// 係数・組み合わせ方が範囲の外のものは、何も記録しない
	FORCEINLINE void CustomBlend( const FCustomBlend& Settings )
	{
		const int32 MaxFactor = static_cast<int32>( EDrawBlendFactor::SrcAlphaSat );
		const int32 MaxOp = static_cast<int32>( EDrawBlendOp::Max );
		if ( !IsInEnumRange( static_cast<int32>( Settings.SrcColor ), MaxFactor ) || !IsInEnumRange( static_cast<int32>( Settings.DestColor ), MaxFactor )
			 || !IsInEnumRange( static_cast<int32>( Settings.SrcAlpha ), MaxFactor ) || !IsInEnumRange( static_cast<int32>( Settings.DestAlpha ), MaxFactor )
			 || !IsInEnumRange( static_cast<int32>( Settings.ColorOp ), MaxOp ) || !IsInEnumRange( static_cast<int32>( Settings.AlphaOp ), MaxOp ) )
		{
			return;
		}
		FDrawCustomBlendData Data;
		Data.SrcColor = static_cast<int32>( Settings.SrcColor );
		Data.DestColor = static_cast<int32>( Settings.DestColor );
		Data.ColorOp = static_cast<int32>( Settings.ColorOp );
		Data.SrcAlpha = static_cast<int32>( Settings.SrcAlpha );
		Data.DestAlpha = static_cast<int32>( Settings.DestAlpha );
		Data.AlphaOp = static_cast<int32>( Settings.AlphaOp );
		Data.Param = NMath::Clamp( Settings.Param, 0, 255 );
		Push( EDrawCommand::CustomBlend, 0, Data );
		CurrentBlendMode = EDrawBlend::Custom;
		CurrentBlendParam = Data.Param;
		CurrentCustomBlend = Settings;
		CurrentCustomBlend.Param = Data.Param;
	}

	// このDrawContextで最後に記録した合成方法（カスタムブレンドのあとは Custom）。初期状態はOpaque。
	FORCEINLINE EDrawBlend GetBlendMode() const { return CurrentBlendMode; }
	// このDrawContextで最後に記録した合成倍率。初期状態は255。
	FORCEINLINE int32 GetBlendParam() const { return CurrentBlendParam; }

	// 最後に記録した合成方法を、まとめて取り出す（あとで SetBlendState で戻すため）
	FORCEINLINE FBlendState GetBlendState() const
	{
		FBlendState State;
		State.Mode = CurrentBlendMode;
		State.Param = CurrentBlendParam;
		State.Custom = CurrentCustomBlend;
		return State;
	}

	// GetBlendState で取り出した合成方法に戻す。Custom なら CustomBlend、それ以外は Blend を記録する
	FORCEINLINE void SetBlendState( const FBlendState& State )
	{
		if ( State.Mode == EDrawBlend::Custom )
		{
			CustomBlend( State.Custom );
		}
		else
		{
			Blend( State.Mode, State.Param );
		}
	}

	// 以降の描画の色に足す色（SetDrawAddColor。Tint で掛けたあとに足される）。各成分は -255..255（負なら引く。範囲の外は収める）。(0, 0, 0) が元の状態。
	// そのオブジェクトの分が終わると、(0, 0, 0) に戻る
	FORCEINLINE void AddColor( int32 R, int32 G, int32 B )
	{
		FDrawAddColorData Data;
		Data.R = NMath::Clamp( R, -255, 255 );
		Data.G = NMath::Clamp( G, -255, 255 );
		Data.B = NMath::Clamp( B, -255, 255 );
		Push( EDrawCommand::AddColor, 0, Data );
	}

	// アルファテスト（SetDrawAlphaTest）。描く色のアルファ値と Reference（0..255。範囲の外は収める）を Compare で比べて、成り立つ画素だけ描く。
	// 切り抜きの画像（葉・柵など）の縁を、半透明にせずに抜くときなどに。AlphaTestOff か、そのオブジェクトの分が終わると、元（しない）に戻る。Compare が範囲の外なら、何も記録しない
	FORCEINLINE void AlphaTest( EDrawCompare Compare, int32 Reference )
	{
		if ( !IsInEnumRange( static_cast<int32>( Compare ), static_cast<int32>( EDrawCompare::Always ) ) )
		{
			return;
		}
		FDrawAlphaTestData Data;
		Data.Enabled = 1;
		Data.Mode = static_cast<int32>( Compare );
		Data.Reference = NMath::Clamp( Reference, 0, 255 );
		Push( EDrawCommand::AlphaTest, 0, Data );
	}

	// アルファテストをやめる
	FORCEINLINE void AlphaTestOff()
	{
		const FDrawAlphaTestData Data;
		Push( EDrawCommand::AlphaTest, 0, Data );
	}

	// 画像を拡大・縮小・回転して描くときの色の決め方（SetDrawMode）。Nearest はギザギザ（ドット絵向き）、Bilinear はなめらか。
	// そのオブジェクトの分が終わると、始める前の状態に戻る。範囲の外の値は、何も記録しない
	FORCEINLINE void Filter( EDrawFilter Mode )
	{
		if ( !IsInEnumRange( static_cast<int32>( Mode ), static_cast<int32>( EDrawFilter::Anisotropic ) ) )
		{
			return;
		}
		FDrawFilterData Data;
		Data.Mode = static_cast<int32>( Mode );
		Push( EDrawCommand::Filter, 0, Data );
	}

	// クリップ範囲を、いまの範囲と (X0, Y0)-(X1, Y1)（右と下の端は含まない）の共通部分にする（SetDrawArea）。入れ子にできて、PopClip で 1 つ前の範囲に戻る
	// （同じ数だけ呼ぶ。スコープで対にするなら FClipScope）。共通部分が空なら、何も描かれない。そのオブジェクトの分が終わると、始める前の範囲に戻る。
	// 値が絶対値 1e7 超のときは、何も記録せず false を返す（対の PopClip も呼ばないこと）
	FORCEINLINE bool PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
	{
		if ( !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ) ) )
		{
			return false;
		}
		FDrawClipData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Push( EDrawCommand::PushClip, 0, Data );
		return true;
	}

	// 1 つ前のクリップ範囲に戻す。PushClip の数より多く呼んでも、何も起きない
	FORCEINLINE void PopClip()
	{
		const FDrawPopClipData Data;
		Push( EDrawCommand::PopClip, 0, Data );
	}

	// 以降の 2D の描画（図形・画像・文字）に掛ける変換（SetTransformTo2D）。画面の座標（ピクセル）に掛かる。アフィン変換だけ（最後の列は無視される）。
	// 2D のカメラ（スクロール・拡大・回転）に使える。ResetTransform2D か、そのオブジェクトの分が終わると、元（変換しない）に戻る。
	// 値が NaN・無限大・絶対値 1e7 超のときは、何も記録しない
	FORCEINLINE void Transform2D( const FMatrix4x4& Matrix )
	{
		FDrawMatrixData Data;
		PackAffine( Data.M, Matrix );
		for ( const float32 Value : Data.M )
		{
			if ( !IsDrawable( Value ) )
			{
				return;
			}
		}
		Push( EDrawCommand::Transform2D, 0, Data );
	}

	// 2D の変換を、元（変換しない）に戻す
	FORCEINLINE void ResetTransform2D()
	{
		Transform2D( FMatrix4x4::Identity );
	}

	//-------------------------------------------------------------------------
	// 描画先（レンダーターゲット）
	//-------------------------------------------------------------------------
	// 描画先を、描き込める画像（CRenderTarget）に替える（SetDrawScreen）。PopTarget か FTargetScope で、1 つ前の描画先へ戻す（入れ子にできる）。
	// 替えた先は、クリップ範囲なし・2D の変換なしから始まり、戻すと、替える前の範囲・変換に戻る。ブレンドなどの状態と、3D のカメラは、替えても変わらない。
	// 画像が無効（nullptr・無効なハンドル）なら、何も記録せず false を返す（対の PopTarget も呼ばないこと）。
	// 描画先にできない画像（ふつうの画像）を渡すと、実行のときに、対の PopTarget まで、描く命令を飛ばす。
	// そのオブジェクトの分が終わると、Pop が足りなくても、始める前の描画先に戻る
	FORCEINLINE bool PushTarget( FImageHandle Target )
	{
		if ( !Target.IsValid() )
		{
			return false;
		}
		FDrawTargetData Data;
		Data.Handle = Target.Value;
		Push( EDrawCommand::PushTarget, 0, Data );
		return true;
	}

	template<typename TImage>
	FORCEINLINE bool PushTarget( const TImage& Target )
	{
		return PushTarget( ToHandle( Target ) );
	}

	// 1 つ前の描画先へ戻す。PushTarget の数より多く呼んでも、何も起きない
	FORCEINLINE void PopTarget()
	{
		const FDrawPopTargetData Data;
		Push( EDrawCommand::PopTarget, 0, Data );
	}

	// いまの描画先の全体を、色（アルファも）で塗りつぶす（ClearDrawScreen）。クリップ範囲・ブレンドには関係なく、全体を、その色そのままにする。
	// 既定は、透明な黒（アルファを持つ描画先を、透明にする）。アルファを持たない描画先は、不透明な色になる
	FORCEINLINE void Clear( const FColor32& Color = FColor32( 0, 0, 0, 0 ) )
	{
		FDrawClearData Data;
		Data.R = Color.r;
		Data.G = Color.g;
		Data.B = Color.b;
		Data.A = Color.a;
		Push( EDrawCommand::Clear, 0, Data );
	}

	// いまの描画先の矩形 (X0, Y0)-(X1, Y1)（右と下の端は含まない）を、画像 Dest へ写す（GetDrawScreenGraph）。
	// Dest は、矩形と同じ大きさ（幅・高さが同じ）の、描き込める画像（CRenderTarget）にする。大きさが違う・矩形が描画先の外にはみ出す場合は、実行のときに、何もしない。
	// 幅・高さが 0 以下、絶対値が 1e7 を超える座標、無効な画像は、何も記録しない
	FORCEINLINE void CopyScreen( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Dest )
	{
		if ( !Dest.IsValid() || X1 <= X0 || Y1 <= Y0 || !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ) ) )
		{
			return;
		}
		FDrawCopyScreenData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Handle = Dest.Value;
		Push( EDrawCommand::CopyScreen, 0, Data );
	}

	template<typename TImage>
	FORCEINLINE void CopyScreen( int32 X0, int32 Y0, int32 X1, int32 Y1, const TImage& Dest )
	{
		CopyScreen( X0, Y0, X1, Y1, ToHandle( Dest ) );
	}

	//-------------------------------------------------------------------------
	// 3D（ワールド座標）
	//-------------------------------------------------------------------------
	// 線分（DrawLine3D）
	FORCEINLINE void Line3D( const FVector3& A, const FVector3& B, const FColor32& Color )
	{
		FDrawLine3DData Data;
		Set3( Data.P0, A );
		Set3( Data.P1, B );
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Line3D, 0, Data );
	}

	// 三角形（DrawTriangle3D）
	FORCEINLINE void Triangle3D( const FVector3& A, const FVector3& B, const FVector3& C, const FColor32& Color, bool bFill = true )
	{
		FDrawTriangle3DData Data;
		Set3( Data.P0, A );
		Set3( Data.P1, B );
		Set3( Data.P2, C );
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Triangle3D, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 球（DrawSphere3D）
	FORCEINLINE void Sphere3D( const FVector3& Center, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true )
	{
		FDrawSphere3DData Data;
		Set3( Data.Center, Center );
		Data.Radius = Radius;
		Data.Div = DivNum;
		Data.Diffuse = Rgb( Diffuse );
		Data.Specular = Rgb( Specular );
		Push( EDrawCommand::Sphere3D, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// カプセル（DrawCapsule3D）
	FORCEINLINE void Capsule3D( const FVector3& A, const FVector3& B, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true )
	{
		FDrawCapsule3DData Data;
		Set3( Data.P0, A );
		Set3( Data.P1, B );
		Data.Radius = Radius;
		Data.Div = DivNum;
		Data.Diffuse = Rgb( Diffuse );
		Data.Specular = Rgb( Specular );
		Push( EDrawCommand::Capsule3D, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 軸に平行な直方体（DrawCube3D）。傾けたいときは、先に WorldMatrix を記録する
	FORCEINLINE void Cube3D( const FVector3& Min, const FVector3& Max, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true )
	{
		FDrawCube3DData Data;
		Set3( Data.Min, Min );
		Set3( Data.Max, Max );
		Data.Diffuse = Rgb( Diffuse );
		Data.Specular = Rgb( Specular );
		Push( EDrawCommand::Cube3D, bFill ? FDrawFlag::Fill : 0, Data );
	}

	// 常にカメラの方を向く画像（DrawBillboard3D。画像の中心が Position）
	FORCEINLINE void Billboard3D( const FVector3& Position, float32 Size, int32 GraphHandle, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		FDrawBillboard3DData Data;
		Set3( Data.Pos, Position );
		Data.Size = Size;
		Data.Angle = AngleRadians;
		Data.Handle = GraphHandle;
		Push( EDrawCommand::Billboard3D, Flip( bFlipX, bFlipY ), Data );
	}

	// 画像を貼った 3D の四角形（ビルボード・向きが固定の板）。向きの決め方と各項目は FDrawQuad3DData を参照。
	// Camera / CameraY は、記録のときにはカメラを知らない。実行のとき（IDrawExecutor）に、そのときのカメラで向きを決める
	FORCEINLINE void Quad3D( const FDrawQuad3DData& Data )
	{
		Push( EDrawCommand::Quad3D, 0, Data );
	}

	// マスクによる消失と境界発光を描く。無効値を含む入力は部分記録せずに拒否する。
	void MaterialQuad3D( const FDrawMaterialQuadData& Data );

	// ノイズ画像の赤緑成分で材質四角形の画像UVを曲げる。範囲外・非有限値は記録しない。
	void DistortedMaterialQuad3D( const FDrawDistortedMaterialQuadData& Data );

	// マスクによる消失と境界発光を点ごとに補間する帯を記録する。無効値は命令を追加せずに拒否する。
	void MaterialRibbon3D( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points );

	// 材質帯のUVを点ごとのノイズでゆがめて記録し、無効値では追加しない。
	// Data: 共有設定と必要な点数。
	// Points: NumPoints個以上の有効な型付き配列。byte payloadは呼出側で完全長を先に検査する。
	void DistortedMaterialRibbon3D( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points );

	// 固定済みワールド頂点を材質付きで記録し、無効値では追加しない。
	// Data: 共有材質と頂点・index数。
	// Vertices: NumVertices個以上の有効な型付き頂点配列。nullなら何も追加しない。
	// Indices: NumIndices個以上の有効な型付きindex配列。nullなら何も追加しない。
	// bUseRegularCulling: falseならnative設定を保ち、trueなら通常meshの裏面除去設定を使う。
	void MaterialMesh3D( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, bool bUseRegularCulling = false );

	// 一材質だけを効果描画し、残りのModel面は実行側のnative経路へ任せる固定命令を記録する。
	bool ModelMaterial( const FDrawModelMaterialData& Data );

	// 頂点バッファ・インデックスバッファの三角形（編集メッシュ）。各項目は FDrawMeshData を参照。World がローカル -> ワールドの変換行列
	FORCEINLINE void Mesh3D( int32 VertexBuffer, int32 IndexBuffer, int32 NumVertices, int32 StartIndex, int32 NumIndices, int32 Texture, const FMatrix4x4& World, bool bTwoSided )
	{
		FDrawMeshData Data;
		Data.VertexBuffer = VertexBuffer;
		Data.IndexBuffer = IndexBuffer;
		Data.NumVertices = NumVertices;
		Data.StartIndex = StartIndex;
		Data.NumIndices = NumIndices;
		Data.Texture = Texture;
		PackAffine( Data.M, World );
		Push( EDrawCommand::Mesh3D, bTwoSided ? FDrawFlag::TwoSided : 0, Data );
	}

	// 次の Model が描くモデル（ModelHandle）に付けたアニメーション 1 つの、再生時間とブレンド率（MV1SetAttachAnimTime / MV1SetAttachAnimBlendRate）。
	// アニメーションを重ねて（クロスフェードなど）描くときは、付けているアニメーションの数だけ記録してから Model する
	FORCEINLINE void ModelAnimLayer( int32 ModelHandle, int32 AttachIndex, float32 AnimTime, float32 BlendRate )
	{
		FDrawModelAnimData Data;
		Data.Handle = ModelHandle;
		Data.AttachIndex = AttachIndex;
		Data.Time = AnimTime;
		Data.Rate = BlendRate;
		Push( EDrawCommand::ModelAnim, 0, Data );
	}

	// 以降の 3D 図形（Line3D〜Cube3D）のローカル -> ワールド変換（SetTransformToWorld）。アフィン変換だけ（最後の列は無視される）
	FORCEINLINE void WorldMatrix( const FMatrix4x4& Matrix )
	{
		FDrawMatrixData Data;
		PackAffine( Data.M, Matrix );
		Push( EDrawCommand::WorldMatrix, 0, Data );
	}

	// MV1 モデル（MV1SetMatrix -> MV1DrawModel）。World がモデルの変換行列。
	// AnimAttachIndex が 0 以上なら、描画の前に MV1SetAttachAnimTime( ModelHandle, AnimAttachIndex, AnimTime ) する
	// （同じモデルを何度も、別の姿勢で描けるように、状態は描画のたびに指定する）
	FORCEINLINE void Model( int32 ModelHandle, const FMatrix4x4& World, int32 AnimAttachIndex = -1, float32 AnimTime = 0.0f )
	{
		FDrawModelData Data;
		Data.Handle = ModelHandle;
		PackAffine( Data.M, World );
		Data.AnimIndex = AnimAttachIndex;
		Data.AnimTime = AnimTime;
		Push( EDrawCommand::Model3D, 0, Data );
	}

private:
	static FORCEINLINE FDrawRgb Rgb( const FColor32& Color ) { return FDrawRgb{ Color.r, Color.g, Color.b, 0 }; }
	static FORCEINLINE uint8 Flip( bool bFlipX, bool bFlipY ) { return static_cast<uint8>( ( bFlipX ? FDrawFlag::FlipX : 0 ) | ( bFlipY ? FDrawFlag::FlipY : 0 ) ); }

	// 描画に渡された画像から、ハンドルを取り出す（記録のとき、1 回読む）。nullptr は、無効なハンドルになる
	static FORCEINLINE FImageHandle ToHandle( FImageHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FImageHandle ToHandle( const IImage* Image )
	{
		return Image != nullptr ? Image->GetImageHandle() : FImageHandle();
	}

	static FORCEINLINE FImageHandle ToHandle( const IImage& Image )
	{
		return Image.GetImageHandle();
	}

	template<typename T>
	static FORCEINLINE FImageHandle ToHandle( const TSharedPtr<T>& Image )
	{
		return ToHandle( Image.Get() );
	}

	// 画像から切り出す矩形として使える値か（大きさが 1 以上。位置と大きさが、実行のときに int で扱える範囲）
	static FORCEINLINE bool IsValidSource( int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH )
	{
		return SrcW > 0 && SrcH > 0 && SrcW <= 10000000 && SrcH <= 10000000 && SrcX >= -10000000 && SrcX <= 10000000 && SrcY >= -10000000 && SrcY <= 10000000;
	}

	// 2D の図形の座標として描ける値か（有限で、実行のときに int にしても溢れない範囲）
	static FORCEINLINE bool IsDrawable( float32 Value ) { return NMath::IsFinite( Value ) && Value >= -1.0e7f && Value <= 1.0e7f; }
	template<typename... TValues>
	static FORCEINLINE bool AllDrawable( TValues... Values ) { return ( IsDrawable( Values ) && ... ); }
	static FORCEINLINE bool IsPositiveThickness( float32 Thickness ) { return NMath::IsFinite( Thickness ) && Thickness > 0.0f && Thickness <= 1.0e4f; }
	// 近似する角数。0 は「半径から決める」。それ以外は 4..512 に収める
	static FORCEINLINE int32 ClampSegments( int32 NumSegments ) { return NumSegments <= 0 ? 0 : NMath::Clamp( NumSegments, 4, 512 ); }
	// 列挙型の値が、0 から最後の値（Max）の範囲に入っているか
	static FORCEINLINE bool IsInEnumRange( int32 Value, int32 Max )
	{
		return Value >= 0 && Value <= Max;
	}

	FORCEINLINE void PushTriangle( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags )
	{
		if ( !AllDrawable( X0, Y0, X1, Y1, X2, Y2 ) || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawTriangleData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.X2 = X2;
		Data.Y2 = Y2;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Triangle, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
	}

	FORCEINLINE void PushQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags )
	{
		if ( !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawQuadData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.X2 = X2;
		Data.Y2 = Y2;
		Data.X3 = X3;
		Data.Y3 = Y3;
		Data.Thickness = Thickness;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Quad, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
	}

	FORCEINLINE void PushOval( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags )
	{
		if ( !AllDrawable( X, Y, Rx, Ry ) || Rx < 0.0f || Ry < 0.0f || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawOvalData Data;
		Data.X = X;
		Data.Y = Y;
		Data.Rx = Rx;
		Data.Ry = Ry;
		Data.Thickness = Thickness;
		Data.NumSegments = NumSegments;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Oval, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
	}

	FORCEINLINE void PushRoundBox( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags )
	{
		if ( !AllDrawable( X0, Y0, X1, Y1, Rx, Ry ) || Rx < 0.0f || Ry < 0.0f || !IsPositiveThickness( Thickness ) )
		{
			return;
		}
		FDrawRoundBoxData Data;
		Data.X0 = X0;
		Data.Y0 = Y0;
		Data.X1 = X1;
		Data.Y1 = Y1;
		Data.Rx = Rx;
		Data.Ry = Ry;
		Data.Thickness = Thickness;
		Data.NumSegments = NumSegments;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::RoundBox, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
	}
	static FORCEINLINE void Set3( float32 ( &Out )[3], const FVector3& V )
	{
		Out[0] = V.x;
		Out[1] = V.y;
		Out[2] = V.z;
	}
	static FORCEINLINE void PackAffine( float32 ( &Out )[12], const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row )
			for ( int32 Column = 0; Column < 3; ++Column )
				Out[Row * 3 + Column] = Matrix.m[Row][Column];
	}

	// コマンド 1 つを末尾に足す。足した分は 0 で埋まってから書くので、隙間にも決まった値が入る（ビット単位で比べられる）
	template<typename TData>
	FORCEINLINE void Push( EDrawCommand Type, uint8 Flags, const TData& Data )
	{
		constexpr int32 DataWords = static_cast<int32>( sizeof( TData ) / 4 );
		TArray<uint32>& Words = Buffer->Words;
		const int32 Begin = Words.AddZeroed( 1 + DataWords );
		uint32* const Out = Words.GetData() + Begin;
		Out[0] = NDrawCommand::MakeHeader( Type, Flags, 1 + DataWords );
		FMemory::Memcpy( Out + 1, &Data, sizeof( TData ) );
	}

private:
	FDrawBuffer* Buffer;
	// Blend呼び出しで更新する、この記録窓口内の現在合成方法。
	EDrawBlend CurrentBlendMode = EDrawBlend::Opaque;
	// 現在合成方法に渡した倍率。
	int32 CurrentBlendParam = 255;
	// 最後に記録したカスタムブレンドの設定（現在の合成方法が Custom のときだけ意味がある）。
	FCustomBlend CurrentCustomBlend;
};

//=============================================================================
// FClipScope
//   コマンドリストにクリップ範囲を足して（PushClip）、スコープを抜けるとき、1 つ前の範囲に戻す（PopClip）。入れ子にできる。
//
//   {
//       FClipScope Clip( CommandList, 100, 100, 300, 200 );   // この中の描画は、(100, 100)-(300, 200) の外へはみ出さない
//       CommandList.Image( 90.0f, 90.0f, Texture );
//   }
//=============================================================================
class FClipScope
{
public:
	FClipScope( FDrawContext& InCommandList, int32 X0, int32 Y0, int32 X1, int32 Y1 )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushClip( X0, Y0, X1, Y1 ) )
	{
	}

	// PushClip が何も記録しなかったとき（値が大きすぎるとき）は、PopClip もしない（外側の範囲を戻してしまわないため）
	~FClipScope()
	{
		if ( bPushed )
		{
			CommandList->PopClip();
		}
	}

	FClipScope( const FClipScope& ) = delete;
	FClipScope& operator=( const FClipScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FTargetScope
//   コマンドリストの描画先を、描き込める画像（CRenderTarget など）に替えて（PushTarget）、スコープを抜けるとき、1 つ前の描画先に戻す（PopTarget）。
//   入れ子にできる。画像が無効なら、何も記録せず、戻すこともしない。
//
//   {
//       FTargetScope Scope( CommandList, Minimap );   // この中の描画は、Minimap に描かれる
//       CommandList.Clear();
//       CommandList.Circle( 64, 64, 30, FColor32::Red );
//   }
//   CommandList.Image( 10.0f, 10.0f, Minimap );       // ふつうの画像として、元の描画先へ貼る
//=============================================================================
class FTargetScope
{
public:
	template<typename TImage>
	FTargetScope( FDrawContext& InCommandList, const TImage& Target )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushTarget( Target ) )
	{
	}

	// PushTarget が何も記録しなかったとき（無効な画像）は、PopTarget もしない（外側の描画先を戻してしまわないため）
	~FTargetScope()
	{
		if ( bPushed )
		{
			CommandList->PopTarget();
		}
	}

	FTargetScope( const FTargetScope& ) = delete;
	FTargetScope& operator=( const FTargetScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

#endif
