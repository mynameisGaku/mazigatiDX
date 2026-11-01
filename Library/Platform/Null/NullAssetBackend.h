// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Math/Color.h"

//=============================================================================
// CNullAssetBackend
//   IAssetBackend の、DxLib を使わない実装。画像・描画先・バッファ・シャドウマップ・シェーダー・フォント・サウンドを、
//   「作ったふり」をして、ハンドルの発行と寸法・範囲の確認だけを行う。--backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   ふりの決まり:
//   - 画像: ファイルがあれば読み込める。大きさは、PNG・BMP・JPEG のヘッダーから読む（読めない形式は 64 x 64）。中身（画素）は持たない。
//     画素から作った画像（CreateTextureFromPixels）だけは、中身を持ち、UpdateTexturePixels で替えられる
//   - 画像の展開（DecodeImage）: ヘッダーから大きさを読み、全画素を灰色（128, 128, 128, 255）にする。ヘッダーを読めなければ失敗
//   - 描画先: 作れる（寸法と設定を覚える）。画素を読むと、描いていない状態（アルファがあれば 0、なければ 255 の黒）。ファイルへの保存は、いつも失敗
//   - モデル: 読み込めない（いつも InvalidHandle）。モデルを使う見本・場面は、モデルなしで動く
//   - シェーダー: DXBC（コンパイル済み）のバイト列なら作れる。ソースのコンパイル（CompileShader）は、引数が正しければ 4 バイトの DXBC の印だけを返す
//   - フォント: 文字の幅は、半角（ASCII と半角カナ）が大きさの半分、それ以外が大きさと同じ。実際のフォントとは違う（レイアウトの確認用）
//   - サウンド: ファイルがあれば読み込める。長さは WAV のヘッダーから読む（それ以外の形式は 1 秒）
//   - 読み込みは、非同期でも、その場で終わる（IsLoading は、いつも false）
//   - ハンドルは、種類ごとに別の表で持つ。終了するときに、解放されていないハンドルの数をログに残す（"Asset handles at shutdown: ..."）。
//     解放し忘れがあれば、警告にする
//=============================================================================
class CNullAssetBackend : public CAppSubsystem, public IAssetBackend
{
public:
	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// 解放されていないハンドルの数を、ログに残す
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// 画像・描画先・CPU 側の画像
	//-------------------------------------------------------------------------
	int32 LoadTexture( const FString& Path, bool bAsync ) override;
	int32 CreateTextureFromMemory( const void* FileImage, usize Size ) override;
	int32 CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height ) override;
	bool GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] ) override;
	bool GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight ) override;
	void DeleteTexture( int32 Texture ) override;

	int32 CreateRenderTarget( const FRenderTargetDesc& Desc ) override;
	bool ReadRenderTargetPixels( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels ) override;
	bool SaveRenderTarget( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options ) override;

	int32 CreateTextureFromPixels( int32 Width, int32 Height, const FColor32* Pixels ) override;
	bool UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels ) override;
	bool DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels ) override;
	bool DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels ) override;
	bool SavePixels( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options ) override;

	//-------------------------------------------------------------------------
	// モデル（読み込めない）
	//-------------------------------------------------------------------------
	int32 LoadModel( const FString& Path, bool bAsync ) override;
	int32 DuplicateModel( int32 Source ) override;
	void DeleteModel( int32 Model ) override;
	int32 GetModelAnimCount( int32 Model ) override;
	FString GetModelAnimName( int32 Model, int32 AnimIndex ) override;
	float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) override;
	int32 AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel ) override;
	void DetachModelAnim( int32 Model, int32 AttachIndex ) override;
	int32 GetModelMeshCount( int32 Model ) override;
	int32 GetModelFrameCount( int32 Model ) override;
	FString GetModelFrameName( int32 Model, int32 FrameIndex ) override;
	bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) override;

	//-------------------------------------------------------------------------
	// 頂点バッファ・インデックスバッファ・シャドウマップ・シェーダー
	//-------------------------------------------------------------------------
	bool CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer ) override;
	void DeleteVertexBuffer( int32 VertexBuffer ) override;
	void DeleteIndexBuffer( int32 IndexBuffer ) override;
	int32 MakeVertexBuffer( int32 NumVertices ) override;
	bool WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices ) override;
	int32 MakeIndexBuffer( int32 NumIndices, bool b32Bit ) override;
	bool WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices ) override;

	int32 MakeShadowMap( int32 Width, int32 Height ) override;
	void DeleteShadowMap( int32 ShadowMap ) override;

	int32 MakeShader( EShaderStage Stage, const void* Code, int32 NumBytes ) override;
	void DeleteShader( int32 Shader ) override;
	bool CompileShader( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage ) override;

	//-------------------------------------------------------------------------
	// フォント
	//-------------------------------------------------------------------------
	int32 MakeFont( const FFontDesc& Desc ) override;
	void DeleteFont( int32 Font ) override;
	int32 MeasureText( int32 Font, const char* Text ) override;
	bool GetFontMetrics( int32 Font, FFontMetrics& OutMetrics ) override;
	bool MeasureTextSize( int32 Font, const char* Text, FTextSize& OutSize ) override;
	bool GetTextGlyphs( int32 Font, const char* Text, TArray<FTextGlyph>& OutGlyphs ) override;
	bool IsFontAvailable( const FString& Face ) override;
	void GetFontNames( TArray<FString>& OutNames ) override;
	bool AddFontCharImage( int32 Font, const char* Character, int32 Image, int32 DrawX, int32 DrawY, int32 Advance ) override;
	void RemoveFontCharImage( int32 Font, const char* Character ) override;
	bool AddFontFile( const FString& Path ) override;
	void RemoveFontFile( const FString& Path ) override;

	//-------------------------------------------------------------------------
	// サウンド・メモリ・非同期
	//-------------------------------------------------------------------------
	int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) override;
	int32 DuplicateSound( int32 Source ) override;
	void DeleteSound( int32 Sound ) override;
	float32 GetSoundLength( int32 Sound ) override;

	FBackendMemoryInfo GetMemoryInfo() override;
	int64 EstimateModelBytes( int32 Model ) override;
	int64 EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode ) override;

	bool IsLoading( int32 Handle ) override;
	int32 GetNumLoading() override;

	//-------------------------------------------------------------------------
	// Null 固有（確認用）
	//-------------------------------------------------------------------------
	// 解放されていないハンドルの数
	int32 GetNumTextures() const;
	int32 GetNumVertexBuffers() const;
	int32 GetNumIndexBuffers() const;
	int32 GetNumShadowMaps() const;
	int32 GetNumShaders() const;
	int32 GetNumFonts() const;
	int32 GetNumSounds() const;
	// 作った画像・描画先・バッファ・シェーダーなどのハンドルの合計
	int32 GetNumLiveHandles() const;

private:
	// 画像 1 つの記録
	struct FTextureRecord
	{
		int32 Width = 0;
		int32 Height = 0;
		// 派生画像（CreateTextureRegion）のとき true。元の画像のハンドルと、元の画像の中での左上の位置
		bool bRegion = false;
		int32 Parent = NAsset::InvalidHandle;
		int32 RegionX = 0;
		int32 RegionY = 0;
		// 描画先として作ったとき true
		bool bRenderTarget = false;
		FRenderTargetDesc TargetDesc;
		// 画素から作った画像だけが持つ中身
		bool bHasPixels = false;
		TArray<FColor32> Pixels;
	};

	struct FIndexBufferRecord
	{
		int32 NumIndices = 0;
		bool b32Bit = false;
	};

	struct FShadowMapRecord
	{
		int32 Width = 0;
		int32 Height = 0;
	};

	struct FFontRecord
	{
		FFontDesc Desc;
		FFontMetrics Metrics;
		// AddFontCharImage で決めた、1 文字（コードポイント）ごとの次の文字までの幅
		TMap<uint32, int32> CharAdvances;
	};

	struct FSoundRecord
	{
		float32 Length = 1.0f;
		ESoundLoadMode Mode = ESoundLoadMode::Memory;
	};

	// 新しいハンドル（1 から増える。種類ごとの表は別だが、番号は共通で、重ならない）
	int32 AllocateHandle();
	// 画像を表に足して、ハンドルを返す
	int32 AddTexture( const FTextureRecord& Record );
	// 1 文字（UTF-8 のコードポイント）の次の文字までの幅
	int32 GetAdvance( const FFontRecord& Font, uint32 CodePoint ) const;
	// 文字列の大きさと、文字ごとの位置を測る（OutGlyphs は、nullptr なら使わない）
	void Layout( const FFontRecord& Font, const char* Text, FTextSize& OutSize, TArray<FTextGlyph>* OutGlyphs ) const;

	int32 NextHandle = 1;
	TMap<int32, FTextureRecord> Textures;
	TMap<int32, int32> VertexBuffers;
	TMap<int32, FIndexBufferRecord> IndexBuffers;
	TMap<int32, FShadowMapRecord> ShadowMaps;
	TMap<int32, EShaderStage> Shaders;
	TMap<int32, FFontRecord> Fonts;
	TMap<int32, FSoundRecord> Sounds;
	TArray<FString> FontFiles;
	// モデルを読み込めないことの警告を、1 回だけ出すため
	bool bWarnedModel = false;
};
