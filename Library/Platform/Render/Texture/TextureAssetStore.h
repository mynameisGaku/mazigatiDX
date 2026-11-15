// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Interfaces/Render/IRenderTargetDrawSource.h"
#include "Library/Core/Render/ComputeTextureFormat.h"
#include "Library/Core/Collection/Map.h"

// アセット番号と画像資源番号の対応を所有する。codec・GPU・ImGuiを知らない。
class CTextureAssetStore : public ITextureDrawSource, public IRenderTargetDrawSource
{
public:
	// 未接続で作る。Initializeまで作成・参照は失敗する。
	CTextureAssetStore() = default;
	// Resourcesは、Shutdownまで生存させる。
	explicit CTextureAssetStore( IRenderTextureResources& InResources );
	// 登録画像の解放要求を資源の所有者へ戻す。
	~CTextureAssetStore() override;
	// 登録番号と所有画像は複写しない。
	CTextureAssetStore( const CTextureAssetStore& ) = delete;
	CTextureAssetStore& operator=( const CTextureAssetStore& ) = delete;
	// 未接続の場合だけ資源の所有先を固定する。既存の登録と番号を保つ。
	bool Initialize( IRenderTextureResources& InResources );
	// 全登録を外して接続を切る。再初期化しても番号は再利用しない。
	void Shutdown();
	// RGBA8を複写して登録する。失敗時は-1。番号を再利用しない。
	int32 Create( int32 Width, int32 Height, const FColor32* Pixels );
	// 描き込める画像（描画先）を登録する。bDepthなら専用の深度も作る。ClearColorで塗って始める。失敗時は-1。
	// Formatは、色の画素形式（RGBA8・RGBA16F・R32Float）。ClearColorは、R32Floatでは赤だけを使う（0〜255を0〜1にする）。
	int32 CreateRenderTarget( int32 Width, int32 Height, bool bDepth, ERenderTargetColorFormat Format, FColor32 ClearColor );
	// 画像を、シェーダーが読むときの形式。描画先は作った形式、ふつうの画像はRGBA8。登録が無ければfalse。
	bool GetShaderFormat( int32 Handle, EComputeTextureFormat& Out ) const;
	// 描画先の画素を読む（浮動小数点の形式は、0〜1に収めて8ビットにする）。描画先でない・読めないときはfalse（出力を保つ）。
	bool ReadRenderTarget( int32 Handle, TArray<FColor32>& OutPixels, int32& OutWidth, int32& OutHeight );
	// 同じ寸法の画像を更新する。失敗時は内容を保つ。
	bool Update( int32 Handle, int32 Width, int32 Height, const FColor32* Pixels );
	// 元の画像（ふつうの画像だけ。描画先・派生画像は不可）の矩形 (X, Y, Width, Height) を共有する、派生画像を登録する。GPU資源は元の画像と同じ。失敗時は-1。
	int32 CreateRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height );
	// 派生画像の矩形を、元の画像の UV（両端を、半 texel だけ内側へ寄せたもの。3Dの面へ渡す）にする。派生画像でない・元が無いときは false。
	bool GetRegionUvBounds( int32 Handle, float32 ( &OutUV )[4] );
	// 登録を外す。GPU完了待ちと実体の解放は資源の所有者が行う。
	void Delete( int32 Handle );
	// すべて登録から外す。次の番号は巻き戻さない。
	void Reset();
	// 有効な登録数。UIのフォント等、別の所有者の画像は数えない。
	int32 Num() const;
	// 有効な画像の寸法。失敗時は両出力を保つ。
	bool GetSize( int32 Handle, int32& OutWidth, int32& OutHeight ) const;
	// 自分の登録番号の、GPU資源の番号。失敗時はOutを保つ（シェーダーへ渡すときに使う）。
	bool GetResource( int32 Handle, FTextureResourceHandle& Out ) const;
	// 自分の登録番号だけを資源のviewへ解決する。失敗時はOutを保つ。
	bool GetTextureDrawView( int32 Handle, FTextureDrawView& Out ) override;
	// 描画先として登録した番号だけを、色と深度の資源へ解決する。失敗時はOutを保つ。
	bool GetRenderTargetDrawView( int32 Handle, FRenderTargetDrawView& Out ) override;
private:
	IRenderTextureResources* Resources = nullptr; // GPU資源の作成・更新・寿命の窓口。
	TMap<int32, FTextureResourceHandle> Textures; // 異なる名前空間の番号を対応させる。
	// 描画先として作った登録の、深度（無ければ無効）。鍵があれば描画先。
	TMap<int32, FTextureResourceHandle> TargetDepths;
	TMap<int32, ERenderTargetColorFormat> TargetFormats; // 描画先の色の形式。
	TMap<int32, int64> TargetSizes; // 描画先の大きさ（幅 << 32 | 高さ）。浮動小数点の描画先は、資源のviewを借りられないので、ここで持つ。
	// 派生画像（CreateRegion）。元の画像の番号と、その中の矩形。GPU資源は持たない（元の画像が消えると、使えなくなる）。
	struct FRegion
	{
		int32 Source = -1;
		int32 X = 0, Y = 0, Width = 0, Height = 0;
	};
	TMap<int32, FRegion> Regions;
	int32 NextHandle = 0; // 解放後も使い回さないアセット番号。
	static constexpr int32 MaxHandles = 1024; // この窓口で同時に登録する上限。
};
