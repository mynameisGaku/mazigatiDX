#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Asset/StaticModelSurfaceData.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Platform/DxLib/DxLibModelMaterialGeometry.h"

//=============================================================================
// CDxLibAssetBackend
//   IAssetBackend の DxLib 実装。アプリで 1 つ、自動で生成される。
//   ゲーム側は直接使わず、CAssetManager を通して読み込む。
//
//   画像 -> LoadGraph / CreateGraphFromMem / DeleteGraph
//   モデル -> MV1LoadModel / MV1DuplicateModel / MV1AttachAnim / MV1DeleteModel
//   フォント -> CreateFontToHandle / DeleteFontToHandle（フォントファイルは AddFontResourceEx の FR_PRIVATE）
//   サウンド -> LoadSoundMem（Stream は DX_SOUNDDATATYPE_FILE）/ DuplicateSoundMem / DeleteSoundMem
//   非同期 -> SetUseASyncLoadFlag / CheckHandleASyncLoad
//=============================================================================
class CDxLibAssetBackend : public CAppSubsystem, public IAssetBackend
{
public:
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	int32 LoadTexture( const FString& Path, bool bAsync ) override;
	int32 CreateTextureFromMemory( const void* FileImage, usize Size ) override;
	// 元画像の指定矩形をDxLibの派生グラフとして作る。
	int32 CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height ) override;
	// DxLibのbase texture上で画像が占める領域をUVへ変換する。
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

	int32 LoadModel( const FString& Path, bool bAsync ) override;
	int32 DuplicateModel( int32 Source ) override;
	void DeleteModel( int32 Model ) override;
	int32 GetModelAnimCount( int32 Model ) override;
	FString GetModelAnimName( int32 Model, int32 AnimIndex ) override;
	float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) override;
	int32 AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel ) override;
	void DetachModelAnim( int32 Model, int32 AttachIndex ) override;
	int32 GetModelMeshCount( int32 Model ) override;
	bool GetModelBounds( int32 Model, float32 ( &OutMin )[3], float32 ( &OutMax )[3] ) override;
	bool GetModelCollisionMesh( int32 Model, TArray<float32>& OutPositions, TArray<uint32>& OutIndices ) override;
	// 元modelを変えず、静的な面・材質をCPU値へ写す。
	bool GetStaticModelSurface( int32 Model, FStaticModelSurfaceData& Out ) override;
	// 指定材質の静的面を不変chunk資源へ登録する。
	int32 CreateModelMaterialGeometry( const FStaticModelSurfaceData& Surface, int32 MaterialIndex ) override;
	// 登録済みの不変chunk資源を解放する。
	void DeleteModelMaterialGeometry( int32 Geometry ) override;
	bool CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer ) override;
	void DeleteVertexBuffer( int32 VertexBuffer ) override;
	void DeleteIndexBuffer( int32 IndexBuffer ) override;
	int32 MakeVertexBuffer( int32 NumVertices ) override;
	bool WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices ) override;
	int32 MakeIndexBuffer( int32 NumIndices, bool b32Bit ) override;
	bool WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices ) override;
	int32 MakeShadowMap( int32 Width, int32 Height ) override;
	void DeleteShadowMap( int32 ShadowMap ) override;
	bool GetShadowMapViewProjection( int32 ShadowMap, float32 ( &OutMatrix )[16] ) override;
	int32 MakeShader( EShaderStage Stage, const void* Code, int32 NumBytes ) override;
	void DeleteShader( int32 Shader ) override;
	bool CompileShader( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage ) override;
	int32 GetModelFrameCount( int32 Model ) override;
	FString GetModelFrameName( int32 Model, int32 FrameIndex ) override;
	bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) override;

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

	int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) override;
	int32 DuplicateSound( int32 Source ) override;
	void DeleteSound( int32 Sound ) override;
	float32 GetSoundLength( int32 Sound ) override;

	FBackendMemoryInfo GetMemoryInfo() override;
	int64 EstimateModelBytes( int32 Model ) override;
	int64 EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode ) override;

	bool IsLoading( int32 Handle ) override;
	int32 GetNumLoading() override;
	// executorと検査がhandleから不変形状を読み取る。
	const FDxLibModelMaterialGeometry* FindModelMaterialGeometry( int32 Geometry ) const;
	// 現在backendが保持する不変形状資源数を返す。
	int32 GetNumModelMaterialGeometries() const;

private:
	// effect描画から読む登録済みの不変形状を所有する。
	TMap<int32, TSharedPtr<const FDxLibModelMaterialGeometry>> ModelMaterialGeometries;
	// 重複を避けて次のbackend内部handleを割り当てる値。
	int32 NextModelMaterialGeometry = 1;
};
