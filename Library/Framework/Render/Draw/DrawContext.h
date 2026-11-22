// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContextShaders.h"

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
//   - 描画の状態（Blend / CustomBlend / Tint / AddColor / AlphaTest / Filter / PushClip / Transform2D）と 3D 図形の変換（WorldMatrix）と描画先（PushTarget）と
//     カメラ（PushCamera）と 3D の描画状態（Depth / Culling / TextureAddress / TextureTransform / Fog / HeightFog / Anisotropy）と
//     ライティング（PushLight / UseLighting / DefaultLight / AmbientLight / SurfaceMaterial / LightingOptions）とシャドウマップ（BeginShadowMap / UseShadowMap）は、そのオブジェクトの分が終わると元に戻る
//   - ライト・カメラ・描画先・深度バッファの使い方は、記録しなければ、呼び出し側が決めたものを使う
//   - グラフィック / モデルのハンドルは、あらかじめ読み込んでおく（読み込みは DxLib の呼び出しなのでメインスレッドで）
//=============================================================================
class FDrawContext : public FDrawContextShaders
{
public:
	explicit FDrawContext( FDrawBuffer& InBuffer ) : FDrawContextShaders( InBuffer ) {}

	//-------------------------------------------------------------------------
	// 3D（ワールド座標）
	//-------------------------------------------------------------------------
	// 線分（DrawLine3D）
	void Line3D( const FVector3& A, const FVector3& B, const FColor32& Color );

	// 三角形（DrawTriangle3D）
	void Triangle3D( const FVector3& A, const FVector3& B, const FVector3& C, const FColor32& Color, bool bFill = true );

	// 球（DrawSphere3D）
	void Sphere3D( const FVector3& Center, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true );

	// カプセル（DrawCapsule3D）
	void Capsule3D( const FVector3& A, const FVector3& B, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true );

	// 軸に平行な直方体（DrawCube3D）。傾けたいときは、先に WorldMatrix を記録する
	void Cube3D( const FVector3& Min, const FVector3& Max, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true );

	// 常にカメラの方を向く画像（DrawBillboard3D。画像の中心が Position）
	void Billboard3D( const FVector3& Position, float32 Size, int32 GraphHandle, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	//-------------------------------------------------------------------------
	// 3D 図形の追加（点・円錐・カメラへ向ける画像・頂点の並び）
	//   位置・大きさ・角度・拡大率が有限でない（NaN・無限大）、画像が無効なら、何も記録しない。ブレンドモード（Blend）と描画の色（Tint）は、そのまま効く。
	//   画像を置くもの（BillboardPivot3D / BillboardQuad3D / Sprite3D / SpriteStretch3D / SpritePivot3D）は、カメラの方へ向き（カメラを回すと、画像も回る）、
	//   Culling に関係なく見え、光を受けず、深度は効く。画像は透過して描く（画像のアルファが効く）。
	//-------------------------------------------------------------------------
	// 点（DrawPixel3D）。1 画素。ブレンド・Bright・深度は効く。フォグと光は効かない
	void Pixel3D( const FVector3& Position, const FColor32& Color );

	// 円錐（DrawCone3D）。Top が先、Bottom が底の中心で、底の円の半径が Radius。底にも面がある。DivNum は円周の分割数。
	// 光を受ける。外側の面（表）と内側の面（裏）でできていて、Culling に従う（FrontFaces では表が描かれず、内側だけが見える。内側の法線は外を向くので、光を受けると真っ暗になる）。
	// Top と Bottom が同じ、Radius が 0 なら、何も描かれない
	void Cone3D( const FVector3& Top, const FVector3& Bottom, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular = FColor32::Black, bool bFill = true );

	// 画像を、カメラの方へ向けて置く（DrawBillboard3D）。Size は画像の横の長さ（ワールド）で、縦は画像の縦横比で決まる。
	// 画像の中の点 (PivotX, PivotY)（0..1、左上が 0, 0。範囲の外でもよい）が Position に来て、AngleRadians（正は反時計回り）の回転は、この点を中心にする。
	// 反転すると、画像の中だけが反転する（位置は同じ）。Size が 0 なら、何も描かれない
	void BillboardPivot3D( const FVector3& Position, float32 Size, FImageHandle Handle, float32 PivotX = 0.5f, float32 PivotY = 0.5f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void BillboardPivot3D( const FVector3& Position, float32 Size, const TImage& Source, float32 PivotX = 0.5f, float32 PivotY = 0.5f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		BillboardPivot3D( Position, Size, ToHandle( Source ), PivotX, PivotY, AngleRadians, bFlipX, bFlipY );
	}

	// 画像の 左上・右上・右下・左下 を、Position から (X0, Y0) (X1, Y1) (X2, Y2) (X3, Y3) だけずれた点へ動かして、自由に変形して置く（DrawModiBillboard3D）。
	// ずれはワールドの長さで、カメラから見て、右が +X、上が +Y。4 つの角が同じ点など、つぶれた形は、何も描かれない
	void BillboardQuad3D( const FVector3& Position, float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, FImageHandle Handle );

	template<typename TImage>
	FORCEINLINE void BillboardQuad3D( const FVector3& Position, float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const TImage& Source )
	{
		BillboardQuad3D( Position, X0, Y0, X1, Y1, X2, Y2, X3, Y3, ToHandle( Source ) );
	}

	// 画像の中心を Position に置いて、カメラの方へ向ける（DrawRotaGraph3D）。大きさは、画像の 1 ピクセルがワールドの Scale の長さ（Scale 0.1 で 32 x 16 の画像なら、3.2 x 1.6）。
	// AngleRadians は、画像の中心まわりの回転（正は反時計回り）。Scale が 0 なら、何も描かれない
	void Sprite3D( const FVector3& Position, FImageHandle Handle, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void Sprite3D( const FVector3& Position, const TImage& Source, float32 Scale = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		Sprite3D( Position, ToHandle( Source ), Scale, AngleRadians, bFlipX, bFlipY );
	}

	// 画像の中心を Position に置いて、横・縦に別々の拡大率で、カメラの方へ向ける（DrawExtendGraph3D）。大きさの決め方は Sprite3D と同じ
	void SpriteStretch3D( const FVector3& Position, FImageHandle Handle, float32 ScaleX, float32 ScaleY );

	template<typename TImage>
	FORCEINLINE void SpriteStretch3D( const FVector3& Position, const TImage& Source, float32 ScaleX, float32 ScaleY )
	{
		SpriteStretch3D( Position, ToHandle( Source ), ScaleX, ScaleY );
	}

	// 画像の中の点 (PivotX, PivotY)（ピクセル。左上が 0, 0。範囲の外でもよい）が Position に来るように、横・縦に別々の拡大率で、カメラの方へ向けて置く（DrawRota2Graph3D）。
	// AngleRadians は、基準点まわりの回転（正は反時計回り）。大きさの決め方は Sprite3D と同じ。反転すると、画像の中だけが反転する（位置は同じ）
	void SpritePivot3D( const FVector3& Position, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false );

	template<typename TImage>
	FORCEINLINE void SpritePivot3D( const FVector3& Position, const TImage& Source, float32 PivotX, float32 PivotY, float32 ScaleX = 1.0f, float32 ScaleY = 1.0f, float32 AngleRadians = 0.0f, bool bFlipX = false, bool bFlipY = false )
	{
		SpritePivot3D( Position, ToHandle( Source ), PivotX, PivotY, ScaleX, ScaleY, AngleRadians, bFlipX, bFlipY );
	}

	// 頂点の並びを、点・線・三角形として描く（DrawPrimitive3D）。つなぎ方は EDrawPrimitive。頂点の数は、つなぎ方に合う数であること
	// （Lines は偶数、Triangles は 3 の倍数、帯・扇は 3 以上、折れ線は 2 以上）。Texture を渡すと、頂点の U, V で貼る（無効な画像なら、何も記録しない）。
	// 位置は、WorldMatrix / PushWorldMatrix の変換を受ける。点・線・三角形のどれも、光は頂点の Normal で受け、フォグも受ける（Pixel3D・Line3D と違う）。
	// Culling に従う（三角形の表は、画面で時計回りに見える面）。
	// 1 つの命令に入る大きさに上限がある（頂点 48 バイト。FDrawPrimitive3DData::GetCommandWords が 65,535 ワード以下）。それを超える大きな形は、分けて記録する。
	// 数が合わない・大きすぎる・数値が有限でない・画像が無効なときは、何も記録せず false を返す
	bool Primitive3D( const FVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type );

	bool Primitive3D( const FVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type, FImageHandle Texture );

	template<typename TImage>
	bool Primitive3D( const FVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type, const TImage& Source )
	{
		return Primitive3D( Vertices, NumVertices, Type, ToHandle( Source ) );
	}

	// インデックスで頂点を選んで描く（DrawPrimitiveIndexed3D）。Indices の数（NumIndices）が、つなぎ方に合う数であること。インデックスは、すべて NumVertices 未満であること。そのほかの規則は Primitive3D と同じ
	bool PrimitiveIndexed3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type );

	bool PrimitiveIndexed3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type, FImageHandle Texture );

	template<typename TImage>
	bool PrimitiveIndexed3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type, const TImage& Source )
	{
		return PrimitiveIndexed3D( Vertices, NumVertices, Indices, NumIndices, Type, ToHandle( Source ) );
	}

	//-------------------------------------------------------------------------
	// 頂点バッファ・インデックスバッファ（GPU に置いた頂点を描く）
	//   頂点バッファ（CVertexBuffer など。IVertexBuffer を実装したもの）とインデックスバッファ（CIndexBuffer など）は、TSharedPtr・ポインター・参照・ハンドル（FVertexBufferHandle / FIndexBufferHandle）のどれでも渡せる。
	//   一度作ったバッファを、何度も描ける（頂点を毎回記録しなくてよい。命令は、バッファの指定だけで小さい）。WorldMatrix / PushWorldMatrix で、位置を変えて、使い回せる。
	//   記録してから、描かれるまで、バッファを手放さないこと。無効なバッファ（nullptr・作れなかった）・無効な画像・範囲やつなぎ方に合わない数のときは、何も記録せず false を返す。
	//   DxLib の実測: 頂点バッファの描画には、Blend と Tint が効かない（頂点の色とアルファで表す）。光・フォグ・Culling・変換・Depth・頂点のアルファは、Primitive3D と同じに効く。
	//   インデックスが指す頂点が、頂点バッファの範囲の外のとき（BaseVertex を足した先も）は、DxLib は失敗を返し、先に処理した三角形だけを描くことがある（描かれるものは決まらないが、落ちない）。収めるのは、呼ぶ側の責任。
	//-------------------------------------------------------------------------
	// 頂点バッファの頂点を、つなぎ方 Type で描く（DrawPrimitive3D_UseVertexBuffer2）。Range の Start 番目から Num 個（Num が負なら最後まで）を使う（BaseVertex は 0 のまま）。
	// 数は、つなぎ方に合うこと（Primitive3D と同じ規則）
	bool PrimitiveBuffer3D( FVertexBufferHandle Vertices, EDrawPrimitive Type, const FBufferRange& Range = FBufferRange() );

	// 画像 Texture を貼る版（頂点の画像の座標で貼る）
	bool PrimitiveBuffer3D( FVertexBufferHandle Vertices, EDrawPrimitive Type, FImageHandle Texture, const FBufferRange& Range = FBufferRange() );

	template<typename TVertices, typename = TEnableIfT<!bIsSame<TVertices, FVertexBufferHandle>>>
	bool PrimitiveBuffer3D( const TVertices& Vertices, EDrawPrimitive Type, const FBufferRange& Range = FBufferRange() )
	{
		return PrimitiveBuffer3D( ToVertexHandle( Vertices ), Type, Range );
	}

	template<typename TVertices, typename TImage, typename = TEnableIfT<!bIsSame<TImage, FBufferRange>>>
	bool PrimitiveBuffer3D( const TVertices& Vertices, EDrawPrimitive Type, const TImage& Texture, const FBufferRange& Range = FBufferRange() )
	{
		return PrimitiveBuffer3D( ToVertexHandle( Vertices ), Type, ToHandle( Texture ), Range );
	}

	// インデックスで頂点を選んで描く（DrawPrimitiveIndexed3D_UseVertexBuffer2）。Range の Start 番目から Num 個（Num が負なら最後まで）のインデックスを使い、各インデックスに BaseVertex を足した頂点を描く。
	// 数は、つなぎ方に合うこと（インデックスの数について。Primitive3D と同じ規則）
	bool PrimitiveIndexedBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, EDrawPrimitive Type, const FBufferRange& Range = FBufferRange() );

	bool PrimitiveIndexedBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, EDrawPrimitive Type, FImageHandle Texture, const FBufferRange& Range = FBufferRange() );

	template<typename TVertices, typename TIndices, typename = TEnableIfT<!bIsSame<TVertices, FVertexBufferHandle> || !bIsSame<TIndices, FIndexBufferHandle>>>
	bool PrimitiveIndexedBuffer3D( const TVertices& Vertices, const TIndices& Indices, EDrawPrimitive Type, const FBufferRange& Range = FBufferRange() )
	{
		return PrimitiveIndexedBuffer3D( ToVertexHandle( Vertices ), ToIndexHandle( Indices ), Type, Range );
	}

	template<typename TVertices, typename TIndices, typename TImage, typename = TEnableIfT<!bIsSame<TImage, FBufferRange>>>
	bool PrimitiveIndexedBuffer3D( const TVertices& Vertices, const TIndices& Indices, EDrawPrimitive Type, const TImage& Texture, const FBufferRange& Range = FBufferRange() )
	{
		return PrimitiveIndexedBuffer3D( ToVertexHandle( Vertices ), ToIndexHandle( Indices ), Type, ToHandle( Texture ), Range );
	}

	// 画像を貼った 3D の四角形（ビルボード・向きが固定の板）。向きの決め方と各項目は FDrawQuad3DData を参照。
	// Camera / CameraY は、記録のときにはカメラを知らない。実行のとき（IDrawExecutor）に、そのときのカメラで向きを決める
	void Quad3D( const FDrawQuad3DData& Data );

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
	void Mesh3D( int32 VertexBuffer, int32 IndexBuffer, int32 NumVertices, int32 StartIndex, int32 NumIndices, int32 Texture, const FMatrix4x4& World, bool bTwoSided );

	// 次の Model が描くモデル（ModelHandle）に付けたアニメーション 1 つの、再生時間とブレンド率（MV1SetAttachAnimTime / MV1SetAttachAnimBlendRate）。
	// アニメーションを重ねて（クロスフェードなど）描くときは、付けているアニメーションの数だけ記録してから Model する
	void ModelAnimLayer( int32 ModelHandle, int32 AttachIndex, float32 AnimTime, float32 BlendRate );

	// ModelAnimLayer のあとに、フレーム Frame（bIncludeChildren なら、その子のフレームすべても）だけのブレンド率を替える（MV1SetAttachAnimBlendRateToFrame）。
	// 体の一部だけを別のアニメーションで動かす（上半身で手を振る、など）ときに使う
	void ModelAnimFrameRate( int32 ModelHandle, int32 AttachIndex, int32 Frame, float32 BlendRate, bool bIncludeChildren = true );

	// 以降の 3D 図形（Line3D〜Cube3D）のローカル -> ワールド変換（SetTransformToWorld）。アフィン変換だけ（最後の列は無視される）
	void WorldMatrix( const FMatrix4x4& Matrix );

	// 以降の 3D 図形（Line3D〜Cube3D）の変換に、Local を掛けて積む（SetTransformToWorld）。Local は、自分の座標 -> 親の座標の変換で、親は、いまの変換（WorldMatrix か、積んだ変換）。
	// 入れ子にできる（親の物を動かすと、子も動く）。PopWorldMatrix か FWorldMatrixScope で、1 つ前の変換へ戻す。そのオブジェクトの分が終わると、
	// PopWorldMatrix が足りなくても、変換なしに戻る。アフィン変換だけ（最後の列は無視される）。値が NaN・無限大のときは、何も記録せず false を返す（対の PopWorldMatrix も呼ばないこと）
	bool PushWorldMatrix( const FMatrix4x4& Local );

	// 1 つ前の 3D 図形の変換へ戻す。PushWorldMatrix の数より多く呼んでも、何も起きない
	void PopWorldMatrix();

	// MV1 モデル（MV1SetMatrix -> MV1DrawModel）。World がモデルの変換行列。
	// AnimAttachIndex が 0 以上なら、描画の前に MV1SetAttachAnimTime( ModelHandle, AnimAttachIndex, AnimTime ) する
	// （同じモデルを何度も、別の姿勢で描けるように、状態は描画のたびに指定する）
	void Model( int32 ModelHandle, const FMatrix4x4& World, int32 AnimAttachIndex = -1, float32 AnimTime = 0.0f );
	// Overridesを通常model描画1命令へ値snapshotして一時適用を要求する。Count0ではnull可。無効値やbuffer上限で失敗し、commandを追加しない。
	bool ModelWithFrameOverrides( int32 ModelHandle, const FMatrix4x4& World, const FDrawModelFrameOverrideData* Overrides, int32 NumOverrides, int32 AnimAttachIndex = -1, float32 AnimTime = 0.0f );

protected:
	// Primitive3D / PrimitiveIndexed3D の記録（使えない値のときは何も記録せず false）。非インデックスは Indices が nullptr・NumIndices が 0
	bool PushPrimitive3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type, int32 Texture );

	// PrimitiveBuffer3D / PrimitiveIndexedBuffer3D の記録（使えない値のときは何も記録せず false）
	bool PushPrimitiveBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, bool bIndexed, EDrawPrimitive Type, int32 Texture, const FBufferRange& Range );
};
