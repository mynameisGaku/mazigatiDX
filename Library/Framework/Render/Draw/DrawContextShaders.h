// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContextLighting.h"

// 自分で書いたシェーダー（頂点と画素の計算）
class FDrawContextShaders : public FDrawContextLighting
{
public:
	explicit FDrawContextShaders( FDrawBuffer& InBuffer ) : FDrawContextLighting( InBuffer ) {}

	//-------------------------------------------------------------------------
	// シェーダー（自分で書いた、頂点と画素の計算）。シェーダーは Framework/Asset/Shader.h、頂点は ShaderVertex.h を参照。
	// 使い方: ① UseVertexShader / UsePixelShader でシェーダーを決める（両方決めないと、シェーダーで描く命令は何も描かない）。
	// ② ShaderConstants で定数を、ShaderTexture で画像を渡す。③ PrimitiveShader3D / PrimitiveIndexedShader3D / ShaderRect で描く。
	// シェーダーの命令は、ふつうの図形・モデルの描き方には触らない（ふつうの Box や Cube3D は、シェーダーを決めたままでも、いつもどおり描ける）。
	// シェーダーで描く図形は、Blend（アルファは、ピクセルシェーダーが出した値。Blend の濃さの指定は効かない）・Culling・深度・クリップ・Filter・テクスチャのアドレスの設定を受ける。
	// 光・フォグ・WorldMatrix・Tint は受けない（シェーダーが、自分の定数で計算する）。
	// このオブジェクトの分が終わると、決めたシェーダー・置いた画像は外れ、定数バッファは片付く（次のオブジェクトに漏れない）。
	// シェーダーは、TSharedPtr・ポインター・参照・ハンドル（FShaderHandle）のどれでも渡せる。記録してから描かれるまで、手放さないこと。
	//-------------------------------------------------------------------------
	// 頂点シェーダーを使わせる（SetUseVertexShader）。頂点シェーダーでないもの・無効なものは、何も記録せず false を返す
	bool UseVertexShader( FShaderHandle Shader );

	template<typename TShader, typename = TEnableIfT<!bIsSame<TShader, FShaderHandle>>>
	bool UseVertexShader( const TShader& Shader )
	{
		return UseVertexShader( ToShaderHandle( Shader ) );
	}

	// ピクセルシェーダーを使わせる（SetUsePixelShader）。ピクセルシェーダーでないもの・無効なものは、何も記録せず false を返す
	bool UsePixelShader( FShaderHandle Shader );

	template<typename TShader, typename = TEnableIfT<!bIsSame<TShader, FShaderHandle>>>
	bool UsePixelShader( const TShader& Shader )
	{
		return UsePixelShader( ToShaderHandle( Shader ) );
	}

	// 頂点シェーダー / ピクセルシェーダーを外す
	void StopUsingVertexShader();
	void StopUsingPixelShader();

	// シェーダーの定数バッファの中身を Data の NumBytes バイトにして、Stage 側のスロット Slot に置く。HLSL では cbuffer Name : register( bN ) で受ける（N が Slot）。
	// Slot は NDrawShader::FirstUserSlot（4）〜 LastUserSlot（13）。0〜3 は DxLib 自身が使うので使えない。NumBytes は 16 の倍数で、16 以上 NDrawShader::MaxConstantBytes（4096）以下。
	// 置いた中身は、次に同じスロットへ置くまで、あとに描くものすべてに効く。描く順に替えられる（同じオブジェクトの中で、1 つずつ別の色で描ける）。
	// 4x4 の行列は、HLSL では row_major float4x4 で受ける（FMatrix4x4 は行ベクトルの流儀で、メモリの並びがそのまま入る）。
	// 使えない値のときは、何も記録せず false を返す
	bool ShaderConstants( EShaderStage Stage, int32 Slot, const void* Data, int32 NumBytes );

	// 構造体 1 つを、そのまま定数バッファの中身にする（大きさが 16 の倍数の、単純な構造体）
	template<typename TValue>
	bool ShaderConstants( EShaderStage Stage, int32 Slot, const TValue& Value )
	{
		static_assert( sizeof( TValue ) % 16 == 0, "the size of a shader constant buffer must be a multiple of 16 bytes (pad the structure)" );
		static_assert( bIsTriviallyCopyable<TValue>, "a shader constant buffer must be a plain structure" );
		return ShaderConstants( Stage, Slot, &Value, static_cast<int32>( sizeof( TValue ) ) );
	}

	// シェーダーが読む画像を、段 Stage（0〜15。HLSL の Texture2D の register( tN ) の N）に置く（SetUseTextureToShader）。
	// シェーダーが宣言した段には、すべて画像を置くこと（宣言が t0 から連続していること。t1 だけ宣言すると、描いたときに DxLib が落ちる）。画像の貼り方は、Filter（最近傍 / 双線形）とテクスチャのアドレスの設定に従う。
	// 無効な画像・段が範囲の外のときは、何も記録せず false を返す
	bool ShaderTexture( int32 Stage, FImageHandle Image );

	template<typename TImage, typename = TEnableIfT<!bIsSame<TImage, FImageHandle>>>
	bool ShaderTexture( int32 Stage, const TImage& Source )
	{
		return ShaderTexture( Stage, ToHandle( Source ) );
	}

	// 段 Stage の画像を外す
	void StopUsingShaderTexture( int32 Stage );

	// ピクセルシェーダーの 2 つ目以降の出力（HLSL の SV_Target1〜SV_Target7。Index は 1〜7）を、描画先 Target に書く（SetRenderTargetToShader。複数の描画先への同時の書き出し）。
	// SV_Target0 は、いまの描画先（PushTarget で決めたもの）に書かれる。色と深度を 1 回の描画で書き分ける、といった使い方をする。
	// 描画先は、いまの描画先と同じ大きさにすること。シェーダーで描く命令だけが書き、ふつうの図形は書かない。PushTarget / PopTarget をまたいでも残り、
	// このオブジェクトの分が終わると外れる。無効な描画先・Index が範囲の外のときは、何も記録せず false を返す
	bool ShaderOutputTarget( int32 Index, FImageHandle Target );

	template<typename TImage, typename = TEnableIfT<!bIsSame<TImage, FImageHandle>>>
	bool ShaderOutputTarget( int32 Index, const TImage& Target )
	{
		return ShaderOutputTarget( Index, ToHandle( Target ) );
	}

	// 出力 Index（1〜7）を、書かないようにする
	void StopUsingShaderOutputTarget( int32 Index );

	// シェーダーで、3D の頂点の並びを描く（DrawPrimitive3DToShader）。つなぎ方は EDrawPrimitive。頂点の数は、つなぎ方に合う数であること（Primitive3D と同じ規則）。
	// 位置は、頂点シェーダーが計算する（ワールド座標を渡して、シェーダーの定数のカメラの行列で変換する、など。DxLib の行列は渡されない）。
	// 1 つの命令に入る大きさに上限がある（頂点 88 バイト。FDrawPrimitiveShader3DData::GetCommandWords が 65,535 ワード以下。頂点だけなら 2,978 個まで）。
	// 数が合わない・大きすぎる・数値が有限でないときは、何も記録せず false を返す
	bool PrimitiveShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type );

	// インデックスで頂点を選んで、シェーダーで描く（DrawPrimitiveIndexed3DToShader）。Indices の数（NumIndices）が、つなぎ方に合う数であること。インデックスは、すべて NumVertices 未満であること
	bool PrimitiveIndexedShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type );

	// 画面の矩形を、ピクセルシェーダーで描く。ポストエフェクトや、画像へのシェーダーの適用に使う。位置と大きさは、描画先の画素の座標（左上が X, Y。大きさが Width x Height）。
	// 頂点シェーダーは、組み込みのものを使う（UseVertexShader の指定は使われず、変わらない。ピクセルシェーダーだけが必要）。画像の座標は、左上が (0, 0)、右下が (1, 1)。
	// ピクセルシェーダーの入力: float4 Position : SV_POSITION、float4 Color : COLOR0（Color）、float2 Uv : TEXCOORD0。
	// 奥行きは 0（いちばん手前）で描き、Depth・Culling・クリップ・Blend の設定を受ける。画面全体にかける効果は、Depth の比較を切っておく。
	// 大きさが 0 以下・数値が有限でない・大きすぎる座標のときは、何も記録せず false を返す
	bool ShaderRect( float32 X, float32 Y, float32 Width, float32 Height, const FColor32& Color = FColor32( 255, 255, 255, 255 ) );
	// ワールドの四角形（ビルボード・向きが固定の板。向きの決め方と各項目は FDrawQuad3DData。Quad3D と同じ）を、UsePixelShader で決めたピクセルシェーダーで描く。
	// 頂点シェーダーは、組み込みのものを使う（ピクセルシェーダーへ、float4 Position : SV_POSITION、float4 Color : COLOR0、float2 Uv : TEXCOORD0、float4 Extra : TEXCOORD1 を渡す）。Handle は使わない
	// （画像は、ShaderTexture で渡す）。Extra は、四角形ごとに渡す 4 つの数（粒子の寿命の位置など。使わなければ、ピクセルシェーダーで宣言しなくてよい）。
	// 奥行きの比べ方・ブレンドは、いまの描画の状態に従う。両面が見える。使えない値（有限でない数・大きさが 0 以下）は、何も記録せず false
	bool ShaderQuad3D( const FDrawQuad3DData& Data, const FVector4& Extra = FVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );

	// 画像の座標も決める版（矩形の左上が (U0, V0)、右下が (U1, V1)）
	bool ShaderRect( float32 X, float32 Y, float32 Width, float32 Height, float32 U0, float32 V0, float32 U1, float32 V1, const FColor32& Color = FColor32( 255, 255, 255, 255 ) );

protected:
	// UseVertexShader / UsePixelShader の記録（種類が違う・無効なシェーダーは何も記録せず false）
	bool PushUseShader( EShaderStage Stage, FShaderHandle Shader );

	// PrimitiveShader3D / PrimitiveIndexedShader3D の記録（使えない値のときは何も記録せず false）。非インデックスは Indices が nullptr・NumIndices が 0
	bool PushPrimitiveShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type );
};
