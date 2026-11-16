// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Render/IGraphicsPipelineResources.h"
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Platform/Render/Texture/TextureAssetStore.h"

//=============================================================================
// CDX12ShaderService
//   利用者のシェーダー（CShader）を、DX12 で描く。次の 3 つを受け持つ。
//   1. シェーダーの登録とコンパイル（IShaderBackend の MakeShader / DeleteShader / CompileShader の実体）。DX12 は Shader Model 5 だけなので、
//      4_0 / 4_1 のソースも 5_0 でコンパイルする（書き方は同じ）。作ったとき、DXBC の中身（頂点の入力・定数バッファ・画像の置き場）を調べて覚える。
//   2. 描画のたびに、シェーダーの組み合わせと描画の状態から、カスタムグラフィックスパイプライン（IGraphicsPipelineResources）を作って使い回す。
//      頂点の並びは FDrawShaderVertex3D（88 バイト）で、頂点シェーダーの入力は、セマンティックの名前から、頂点の中の位置を決める
//      （POSITION / POSITION1 / NORMAL / TANGENT / BINORMAL / COLOR0 / COLOR1 / TEXCOORD0 / TEXCOORD1）。
//   3. 頂点・添字・定数の GPU バッファを、描くたびに作り（IRenderBufferResources）、描いたあとすぐに手放す（GPU の完了までは実体が残る）。
//   ゲームスレッドから使う。GPU の型は、このクラスの中に閉じる（窓口は IShaderDrawSource）。
//
//   できないこと: ピクセルシェーダーの複数出力（SV_Target1〜）、頂点シェーダーが読む画像、画像の外側・補間が段ごとに違う読み方（s0 の 1 つだけ）、
//   頂点シェーダーと ピクセルシェーダーで、同じ番号の定数バッファに、別の中身を置くこと（頂点側の中身が使われる）、合成は通常のアルファだけ。
//=============================================================================
class CDX12ShaderService : public IShaderDrawSource
{
public:
	CDX12ShaderService() = default;
	CDX12ShaderService( const CDX12ShaderService& ) = delete;
	CDX12ShaderService& operator=( const CDX12ShaderService& ) = delete;
	~CDX12ShaderService() override;

	// 窓口を借りる。借用先は Shutdown まで生き続けること。二重の初期化は、何もしない
	void Initialize( IGraphicsPipelineResources* InPipelines, IRenderBufferResources* InBuffers, CTextureAssetStore* InTextures );
	// 作ったパイプラインと、空の画像を手放す（GPU の完了を待つのは、所有者）
	void Shutdown();

	// コンパイル済みの DXBC（Shader Model 5。種類が合うもの）から登録する。作れなければ -1
	int32 Make( EShaderStage Stage, const void* Code, int32 NumBytes );
	// 番号を空け、そのシェーダーを使ったパイプラインも手放す
	void Delete( int32 Shader );
	// HLSL のソースをコンパイルする。失敗したら false で、OutMessage に理由
	bool Compile( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage );
	// 登録してあるシェーダーの数
	int32 Num() const { return Shaders.Num(); }
	// 影の絵を手放したとき、覚えてある値を消す
	void ForgetShadowMap( int32 ShadowMap ) { ShadowPasses.Remove( ShadowMap ); }

	bool IsShaderValid( int32 Shader, EShaderStage Stage ) const override;
	bool DrawShader( const FShaderDrawRequest& Request, IRenderGeometry3D& Geometry ) override;
	void SetShadowPass( int32 ShadowMap, const FMatrix4x4& LightViewProjection, float32 AdjustDepth, int32 Width, int32 Height ) override;
	bool GetShadowPass( int32 ShadowMap, FMatrix4x4& OutLightViewProjection, float32& OutAdjustDepth, int32& OutWidth, int32& OutHeight ) const override;

private:
	struct FInput
	{
		FString Semantic;
		uint32 Index = 0;
		uint32 Mask = 0;
	};
	struct FConstantInfo
	{
		uint32 Binding = 0;
		uint32 Size = 0;
	};
	// 登録したシェーダー 1 つと、調べた中身
	struct FShaderRecord
	{
		EShaderStage Stage = EShaderStage::Vertex;
		TArray<uint8> Code;
		TArray<FInput> Inputs;          // 頂点シェーダーの入力
		TArray<FConstantInfo> Constants;
		TArray<uint32> Textures;        // Texture2D の t 番号
		int32 NumOutputs = 0;           // ピクセルシェーダーの出力の数
		bool bUnsupportedResource = false;
	};
	struct FPipelineEntry
	{
		FGraphicsPipelineHandle Handle;
		int32 VertexShader = -1;
		int32 PixelShader = -1;
	};

	// DXBC を調べて Record を埋める。種類・Shader Model が合わなければ false
	bool Reflect( EShaderStage Stage, const uint8* Code, int32 NumBytes, FShaderRecord& Record ) const;
	// 組み込みのシェーダー（頂点・ピクセル）を、初めて使うときに作る
	void BuildBuiltIns();
	const FShaderRecord* GetBuiltIn( EBuiltInVertexShader Kind );
	const FShaderRecord* GetBuiltInPixel( EBuiltInPixelShader Kind );
	// 空の画像（段に何も置いていないとき、読むと 0 になる）の資源を返す
	bool GetEmptyTexture( FTextureResourceHandle& Out );
	// 状態に合うパイプラインを、作って（あれば使い回して）返す。作れなければ無効
	FGraphicsPipelineHandle GetPipeline( const FShaderDrawRequest& Request, const FShaderRecord& Vertex, const FShaderRecord& Pixel, const TArray<FGraphicsBufferSlot>& BufferSlots, const TArray<FGraphicsTextureSlot>& TextureSlots );

	IGraphicsPipelineResources* Pipelines = nullptr;
	IRenderBufferResources* Buffers = nullptr;
	CTextureAssetStore* Textures = nullptr;
	TMap<int32, FShaderRecord> Shaders;
	int32 NextShader = 0;
	TMap<FString, FPipelineEntry> PipelineCache;   // 鍵は、シェーダーと状態
	FShaderRecord BuiltInRect;
	FShaderRecord BuiltInQuad;
	FShaderRecord BuiltInLit;
	FShaderRecord BuiltInShadowDepth;
	FShaderRecord BuiltInShadowLit;
	FShaderRecord BuiltInShadowLitTextured;
	FShaderRecord BuiltInFogTextured;
	FShaderRecord BuiltInImageFilter;
	FShaderRecord BuiltInImageBlend;
	FShaderRecord BuiltInPixelLitVertex;
	FShaderRecord BuiltInPixelLit;
	FShaderRecord BuiltInPixelLitTextured;
	FShaderRecord BuiltInEffectMaterialVertex;
	FShaderRecord BuiltInEffectMaterial;
	// 影の絵ごとの、最後に描いたときの値
	struct FShadowPass
	{
		FMatrix4x4 LightViewProjection;
		float32 AdjustDepth = 0.0f;
		int32 Width = 0;
		int32 Height = 0;
	};
	TMap<int32, FShadowPass> ShadowPasses;
	bool bBuiltInTried = false;
	int32 EmptyTexture = -1;                       // 空の画像のアセット番号
};
