// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Core/Memory/SharedPtr.h"

//=============================================================================
// CShader
//   シェーダー（画面に描く計算を、自分で書いた小さなプログラム）。頂点シェーダー（頂点 1 つごとに位置を計算する）と、ピクセルシェーダー（画素 1 つごとに色を計算する）がある。
//   書き方は HLSL（DirectX のシェーダー言語）。DxLib の Direct3D 11 で動く（シェーダーモデル 4.0 / 4.1 / 5.0）。
//   ファイルから読むアセットではないので、CAssetManager のキャッシュには入らない。持っている間は有効で、消えるとき、解放する（メインスレッド以外で消えたら、次の更新で解放する）。
//   作るのは、メインスレッドだけ（DxLib がスレッドセーフでないため）。それ以外のスレッドからは nullptr を返す。
//
//   作り方は 3 つ:
//     Compile / CompileFile  HLSL のソース（またはファイル）を、実行のときにコンパイルして作る（Windows に入っている d3dcompiler_47.dll を使う。無い環境では失敗する）。
//                            失敗したら nullptr で、OutMessage に理由（行番号つき）が入る。
//     Create / LoadFile      コンパイル済みのバイト列（またはファイル。fxc の出力）から作る。コンパイラーが無くても使える
//
//   描くときは、FDrawContext に記録する: UseVertexShader / UsePixelShader でシェーダーを決め、ShaderConstants で定数を渡し、ShaderTexture で画像を渡して、
//   PrimitiveShader3D / PrimitiveIndexedShader3D / ShaderRect で描く。記録してから描かれるまで、シェーダーを手放さないこと。
//
//   頂点シェーダーの入力（HLSL）は、FShaderVertex3D の並びのとおりに、先頭から飛ばさずに宣言する（DxLib が、宣言した順に詰めて読むため。途中で止めるのは構わない）:
//       struct VertexInput
//       {
//           float3 Position : POSITION;    float4 SubPosition : POSITION1;
//           float3 Normal : NORMAL;        float3 Tangent : TANGENT;      float3 Binormal : BINORMAL;
//           float4 Diffuse : COLOR0;       float4 Specular : COLOR1;
//           float2 Uv : TEXCOORD0;         float2 SubUv : TEXCOORD1;
//       };
//   ShaderRect（画面の矩形に、ピクセルシェーダーを掛ける。ポストエフェクト用）は、組み込みの頂点シェーダーを使うので、ピクセルシェーダーだけを書けばよい。
//   ピクセルシェーダーの入力は: float4 Position : SV_POSITION;  float4 Color : COLOR0;  float2 Uv : TEXCOORD0;（Uv は、矩形の左上が (0, 0)、右下が (1, 1)）
//   DxLib の 2D の描画（DrawPrimitive2DToShader）は、頂点シェーダーの中身を使わない（実測）ので、2D の頂点は用意していない。
//   画像は、HLSL の Texture2D を t0 から連続して宣言する（t1 だけを宣言すると落ちる）。定数バッファは、cbuffer を register( b4 ) 〜 register( b13 ) に置く。
//
//   TSharedPtr<CShader> Vertex = CShader::Compile( EShaderStage::Vertex, VertexSource, "main", &Message );
//   TSharedPtr<CShader> Pixel = CShader::Compile( EShaderStage::Pixel, PixelSource, "main", &Message );
//   FShaderScope Shaders( Draw, Vertex, Pixel );
//   Draw.ShaderRect( 0.0f, 0.0f, 640.0f, 480.0f );
//=============================================================================
class CShader final : public IShader
{
public:
	// コンパイル済みのシェーダー（バイト列）から作る。バイト列が空・形式が合わない・種類が違う・メインスレッドでない・作れなかったときは nullptr
	static TSharedPtr<CShader> Create( EShaderStage Stage, const void* Code, int32 NumBytes );

	// コンパイル済みのシェーダーのファイルから作る。ファイルが読めないときも nullptr
	static TSharedPtr<CShader> LoadFile( EShaderStage Stage, const FString& Path );

	// HLSL のソースをコンパイルして作る。EntryPoint は入り口の関数の名前、ShaderModel は "4_0"（既定）・"4_1"・"5_0"。
	// 失敗したら nullptr で、OutMessage（nullptr でもよい）に理由が入る。メインスレッドだけ
	static TSharedPtr<CShader> Compile( EShaderStage Stage, const FString& Source, const char* EntryPoint = "main", FString* OutMessage = nullptr, const char* ShaderModel = "4_0" );

	// HLSL のファイルをコンパイルして作る。ファイルの中の #include は、そのファイルのあるフォルダーから探す
	static TSharedPtr<CShader> CompileFile( EShaderStage Stage, const FString& Path, const char* EntryPoint = "main", FString* OutMessage = nullptr, const char* ShaderModel = "4_0" );

	~CShader() override;
	CShader( const CShader& ) = delete;
	CShader& operator=( const CShader& ) = delete;

	EShaderStage GetStage() const { return Stage; }

	FShaderHandle GetShaderHandle() const override;

private:
	CShader( int32 InShader, EShaderStage InStage )
		: Shader( InShader )
		, Stage( InStage )
	{
	}

	int32 Shader = -1;
	EShaderStage Stage = EShaderStage::Vertex;
};
