// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"

//=============================================================================
// IRenderResourceBackend
//   描画の資源（頂点バッファ・インデックスバッファ・シャドウマップ・シェーダー）を作る・破棄する窓口。描くための資源を作る側が知る
//   すべての関数はメインスレッドだけから呼ばれる（バックエンドがスレッドセーフとは限らない）。実装は IAssetBackend を通して持つ
//=============================================================================
class IRenderResourceBackend
{
public:
	virtual ~IRenderResourceBackend() = default;

	//-------------------------------------------------------------------------
	// メッシュ（頂点バッファ・インデックスバッファ。編集メッシュを描くため）
	//-------------------------------------------------------------------------
	// 頂点（位置・法線・画像の座標）と、三角形のインデックス（3 つで 1 つ）から、描画用のバッファを作る。成功したら true（OutVertexBuffer / OutIndexBuffer が有効）。
	// バッファは、描画コマンド（Mesh3D）が、ハンドルで使う。使い終わったら、DeleteVertexBuffer / DeleteIndexBuffer で解放する
	virtual bool CreateMeshBuffers( const FMeshVertex* /*Vertices*/, int32 /*NumVertices*/, const uint32* /*Indices*/, int32 /*NumIndices*/, int32& /*OutVertexBuffer*/, int32& /*OutIndexBuffer*/ ) { return false; }
	virtual void DeleteVertexBuffer( int32 /*VertexBuffer*/ ) {}
	virtual void DeleteIndexBuffer( int32 /*IndexBuffer*/ ) {}

	// 汎用の頂点バッファ・インデックスバッファ（CVertexBuffer / CIndexBuffer）。バッファは、メインスレッドだけで作り・書き換える。解放は DeleteVertexBuffer / DeleteIndexBuffer
	// 頂点 NumVertices 個の空の（0 で埋まった）頂点バッファを作る。作れなければ負（NAsset::InvalidHandle）
	virtual int32 MakeVertexBuffer( int32 /*NumVertices*/ ) { return -1; }
	// 頂点バッファの StartIndex から、頂点 NumVertices 個を書く。範囲が収まらない・無効なハンドルなら false
	virtual bool WriteVertexBuffer( int32 /*VertexBuffer*/, int32 /*StartIndex*/, const FDrawVertex3D* /*Vertices*/, int32 /*NumVertices*/ ) { return false; }
	// インデックス NumIndices 個の空のインデックスバッファを作る（b32Bit が true なら 32 ビット、false なら 16 ビット）。作れなければ負
	virtual int32 MakeIndexBuffer( int32 /*NumIndices*/, bool /*b32Bit*/ ) { return -1; }
	// インデックスバッファの StartIndex から、インデックス NumIndices 個（バッファが 16 ビットなら uint16、32 ビットなら uint32 の並び）を書く。範囲が収まらない・無効なハンドルなら false
	virtual bool WriteIndexBuffer( int32 /*IndexBuffer*/, int32 /*StartIndex*/, const void* /*Indices*/, int32 /*NumIndices*/ ) { return false; }

	//-------------------------------------------------------------------------
	// シャドウマップ（影の絵。CShadowMap）。メインスレッドだけで作る。解放は DeleteShadowMap
	//-------------------------------------------------------------------------
	// 縦横 Width x Height（それぞれ 1 以上、NDrawShadowMap::MaxSize 以下）のシャドウマップを作る。作れなければ負（NAsset::InvalidHandle）
	virtual int32 MakeShadowMap( int32 /*Width*/, int32 /*Height*/ ) { return -1; }
	virtual void DeleteShadowMap( int32 /*ShadowMap*/ ) {}
	// 最後に BeginShadowMap で影を描いたときの、光から見た行列（ビュー行列 x 射影行列。16 個の float。フレームワークの FMatrix4x4 と同じ、行ベクトルの流儀）を取り出す。
	// まだ一度も影を描いていない・無効なハンドルなら false
	virtual bool GetShadowMapViewProjection( int32 /*ShadowMap*/, float32 ( & /*OutMatrix*/ )[16] ) { return false; }

	//-------------------------------------------------------------------------
	// シェーダー（CShader）。メインスレッドだけで作る。解放は DeleteShader
	//-------------------------------------------------------------------------
	// コンパイル済みのシェーダー（バイト列）から作る。作れなければ負（NAsset::InvalidHandle）。形式が合わないバイト列・種類が違うもの（頂点用をピクセル用として読むなど）は作れない
	virtual int32 MakeShader( EShaderStage /*Stage*/, const void* /*Code*/, int32 /*NumBytes*/ ) { return -1; }
	virtual void DeleteShader( int32 /*Shader*/ ) {}
	// HLSL のソース（UTF-8。NumBytes バイト）を、コンパイル済みのシェーダーにする。SourceName は、メッセージと #include の探し先に使う名前（ファイルの名前）、
	// EntryPoint は入り口の関数の名前、ShaderModel は "4_0" など。失敗したら false で、OutMessage に理由（ソースの行番号つき）を入れる。コンパイラーが使えない実装は、いつも false
	virtual bool CompileShader( EShaderStage /*Stage*/, const char* /*Source*/, int32 /*NumBytes*/, const char* /*SourceName*/, const char* /*EntryPoint*/, const char* /*ShaderModel*/, TArray<uint8>& /*OutCode*/, FString& /*OutMessage*/ ) { return false; }
};
