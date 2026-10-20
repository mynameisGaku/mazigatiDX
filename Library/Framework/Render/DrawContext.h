// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_RENDER_DRAWCONTEXT_H
#define FRAMEWORK_RENDER_DRAWCONTEXT_H

#include <cstring>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawRibbon.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FDrawBuffer
//   描画コマンドの入れ物（4 バイト単位の並び。形式は DrawCommand.h を参照）。
//=============================================================================
struct FDrawBuffer
{
	std::vector<uint32> Words;

	// 中身を空にする（確保した領域は残す。毎フレーム使い回すため）
	void Clear() { Words.clear(); }
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
//   - ブレンドモード（Blend）と 3D 図形の変換（WorldMatrix）は、そのオブジェクトの分が終わると元に戻る
//   - カメラ・ライト・Zバッファ・描画先は記録しない（呼び出し側が決めたものを使う）
//   - グラフィック / モデルのハンドルは、あらかじめ読み込んでおく（読み込みは DxLib の呼び出しなのでメインスレッドで）
//=============================================================================
class FDrawContext
{
public:
	explicit FDrawContext( FDrawBuffer& InBuffer ) : Buffer( &InBuffer ) {}

	// これまでに記録したワード数（システムの実行機構が、オブジェクトごとの範囲を決めるのに使う）
	FORCEINLINE int32 GetNumWords() const { return static_cast<int32>( Buffer->Words.size() ); }

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

	// 以降の描画に掛ける色（SetDrawBright）。白に戻すと元の色。そのオブジェクトの分が終わると白に戻る
	FORCEINLINE void Tint( const FColor32& Color )
	{
		FDrawBrightData Data;
		Data.Color = Rgb( Color );
		Push( EDrawCommand::Bright, 0, Data );
	}

	// 以降のブレンドモードを変える（SetDrawBlendMode）。Param は Alpha のときの不透明度（0..255）など
	FORCEINLINE void Blend( EDrawBlend Mode, int32 Param = 255 )
	{
		FDrawBlendData Data;
		Data.Mode = static_cast<int32>( Mode );
		Data.Param = Param;
		Push( EDrawCommand::Blend, 0, Data );
		CurrentBlendMode = Mode;
		CurrentBlendParam = Param;
	}

	// このDrawContextで最後に記録した合成方法。初期状態はOpaque。
	FORCEINLINE EDrawBlend GetBlendMode() const { return CurrentBlendMode; }
	// このDrawContextで最後に記録した合成倍率。初期状態は255。
	FORCEINLINE int32 GetBlendParam() const { return CurrentBlendParam; }

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
		constexpr size_t DataWords = sizeof( TData ) / 4;
		std::vector<uint32>& Words = Buffer->Words;
		const size_t Begin = Words.size();
		Words.resize( Begin + 1 + DataWords );
		Words[Begin] = NDrawCommand::MakeHeader( Type, Flags, static_cast<int32>( 1 + DataWords ) );
		std::memcpy( &Words[Begin + 1], &Data, sizeof( TData ) );
	}

private:
	FDrawBuffer* Buffer;
	// Blend呼び出しで更新する、この記録窓口内の現在合成方法。
	EDrawBlend CurrentBlendMode = EDrawBlend::Opaque;
	// 現在合成方法に渡した倍率。
	int32 CurrentBlendParam = 255;
};

#endif
