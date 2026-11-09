// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "VendorModules/ImGui/imgui.h"
#include "Library/Platform/Render/Geometry/GeometryBufferStore.h"

class IPlatform;

// 実画像だけを登録する。モデル・音声・GPUの独自シェーダーは未対応として返す。
class CDX12AssetBackendSubsystem : public CAppSubsystem, public IAssetBackend, public ITextureDrawSource, public IGeometryBufferSource, public ITickable
{
public:
	// DX12が選ばれた場合だけ作る。
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// GPUと文脈を先に作る。
	void Initialize( FSubsystemCollection& Collection ) override;
	// GPUを待ってから画像を解放する。プラットフォームより先に呼ばれる。
	void Deinitialize() override;
	// 前フレームまで使った画像の遅延解放。
	void Tick( float32 DeltaTime ) override;
	// 画像を同期読み込みする。bAsyncでもその場で完了する。
	int32 LoadTexture( const FString& Path, bool bAsync ) override;
	// WICが対応するファイル内容を展開して登録する。失敗時は-1。
	int32 CreateTextureFromMemory( const void* FileImage, usize Size ) override;
	// 有効な登録画像だけ大きさを返す。
	bool GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight ) override;
	// このフレームの描画が終わるまで実画像の解放を延期する。
	void DeleteTexture( int32 Texture ) override;
	// RGBA8の画素をコピーしてGPUへ送る。不正入力では-1。
	int32 CreateTextureFromPixels( int32 Width, int32 Height, const FColor32* Pixels ) override;
	// 同じ寸法の登録画像を更新する。不正入力では変更しない。
	bool UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels ) override;
	// メモリの画像をWICでRGBA8へ展開する。失敗時は出力を保つ。
	bool DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels ) override;
	// ファイルを展開する。失敗時は出力を保つ。
	bool DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels ) override;
	// 有効な画像だけ描画側へ貸す。
	bool GetTextureDrawView( int32 Handle, FTextureDrawView& Out ) override;
	// 編集メッシュを共通の形状保管へ変換する。失敗時は両出力を保つ。
	bool CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer ) override;
	// ゼロ頂点を登録する。不正な数・保持上限超過は-1。
	int32 MakeVertexBuffer( int32 NumVertices ) override;
	// 検査済みの頂点だけを書き込む。失敗時は内容を保つ。
	bool WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices ) override;
	// 頂点の登録を解放する。
	void DeleteVertexBuffer( int32 VertexBuffer ) override;
	// ゼロindexと入力形式を登録する。
	int32 MakeIndexBuffer( int32 NumIndices, bool b32Bit ) override;
	// 16／32bitのindexを展開して更新する。失敗時は内容を保つ。
	bool WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices ) override;
	// indexの登録を解放する。
	void DeleteIndexBuffer( int32 IndexBuffer ) override;
	// 同期の命令解釈にだけ頂点を貸す。
	bool GetVertexBufferView( int32 Handle, FGeometryVertexBufferView& Out ) override;
	// 同期の命令解釈にだけindexを貸す。
	bool GetIndexBufferView( int32 Handle, FGeometryIndexBufferView& Out ) override;
	// GetMemoryInfoの未対応時の既定値。出力引数と外部状態は変更しない。
	FBackendMemoryInfo GetMemoryInfo() override;
	// EstimateModelBytesの未対応時の既定値。出力引数と外部状態は変更しない。
	int64 EstimateModelBytes( int32 Model ) override;
	// EstimateSoundBytesの未対応時の既定値。出力引数と外部状態は変更しない。
	int64 EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode ) override;
	// IsLoadingの未対応時の既定値。出力引数と外部状態は変更しない。
	bool IsLoading( int32 Handle ) override;
	// GetNumLoadingの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 GetNumLoading() override;
	// LoadModelの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 LoadModel( const FString& Path, bool bAsync ) override;
	// DuplicateModelの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 DuplicateModel( int32 Source ) override;
	// DeleteModelの未対応時の既定値。出力引数と外部状態は変更しない。
	void DeleteModel( int32 Model ) override;
	// GetModelAnimCountの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 GetModelAnimCount( int32 Model ) override;
	// GetModelAnimNameの未対応時の既定値。出力引数と外部状態は変更しない。
	FString GetModelAnimName( int32 Model, int32 AnimIndex ) override;
	// GetModelAnimLengthの未対応時の既定値。出力引数と外部状態は変更しない。
	float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) override;
	// AttachModelAnimの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel ) override;
	// DetachModelAnimの未対応時の既定値。出力引数と外部状態は変更しない。
	void DetachModelAnim( int32 Model, int32 AttachIndex ) override;
	// GetModelMeshCountの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 GetModelMeshCount( int32 Model ) override;
	// GetModelFrameCountの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 GetModelFrameCount( int32 Model ) override;
	// GetModelFrameNameの未対応時の既定値。出力引数と外部状態は変更しない。
	FString GetModelFrameName( int32 Model, int32 FrameIndex ) override;
	// SaveModelToMV1の未対応時の既定値。出力引数と外部状態は変更しない。
	bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) override;
	// MakeFontの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 MakeFont( const FFontDesc& Desc ) override;
	// DeleteFontの未対応時の既定値。出力引数と外部状態は変更しない。
	void DeleteFont( int32 Font ) override;
	// MeasureTextの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 MeasureText( int32 Font, const char* Text ) override;
	// AddFontFileの未対応時の既定値。出力引数と外部状態は変更しない。
	bool AddFontFile( const FString& Path ) override;
	// RemoveFontFileの未対応時の既定値。出力引数と外部状態は変更しない。
	void RemoveFontFile( const FString& Path ) override;
	// LoadSoundの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) override;
	// DuplicateSoundの未対応時の既定値。出力引数と外部状態は変更しない。
	int32 DuplicateSound( int32 Source ) override;
	// DeleteSoundの未対応時の既定値。出力引数と外部状態は変更しない。
	void DeleteSound( int32 Sound ) override;
	// GetSoundLengthの未対応時の既定値。出力引数と外部状態は変更しない。
	float32 GetSoundLength( int32 Sound ) override;
private:
	// 登録済みの画像だけ返す。所有権は移さない。
	ImTextureData* FindTexture( int32 Handle );
	// 遅延解放する画像と最後に使ったフレーム。
	struct FRetiredTexture
	{
		ImTextureData* Texture = nullptr; // 所有する画像。
		uint64 Frame = 0; // 解放を頼まれたフレーム。
	};
	// GPU待ち後に画像を破棄する。
	void DestroyTexture( ImTextureData* Texture );
	FGeometryBufferStore Buffers; // APIに依存しない登録形状。
	TMap<int32, ImTextureData*> Textures; // 登録番号から実画像への表。
	TArray<FRetiredTexture> Retired; // 描画の終了待ち。
	IPlatform* Platform = nullptr; // 借用するフレーム進行。
	static constexpr int32 MaxTextureHandles = 1024; // フォント用の記述子を別に残す登録上限。
	int32 NextHandle = 0; // 再利用しない登録番号。
};
