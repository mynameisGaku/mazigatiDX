// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Deferred/GBufferView.h"

class CAssetSubsystem;
class FDrawContext;

// G-buffer の 1 枚ごとの意味（並びは、ジオメトリのパスの SV_Target0〜3 と、ライティングのパスの画像の段 0〜3 に、そのまま対応する）
enum class EGBufferTarget : uint8
{
	Albedo,   // 地の色 rgb（8 ビット）。3D の描画で奥行きバッファを持つ、ジオメトリのパスの描き先
	Normal,   // 世界の法線 xyz（16 ビットの浮動小数点。長さ 1）。何も描かれていない画素は 0
	Depth,    // カメラの前向きの奥行き / Far（32 ビットの浮動小数点 1 チャンネル）。何も描かれていない画素は 1
	Emission, // 自分で光る色 rgb（16 ビットの浮動小数点。1 を超える明るさが残る）
};

//=============================================================================
// CGBuffer
//   Deferred（後から光を当てる）描画の、G-buffer（地の色・法線・深度・自分で光る色）の描画先を持つ。
//   ジオメトリのパスで、物の面の情報を 1 回の描画で 4 枚へ書き（MRT）、ライティングのパスで、その画像を読んで、画素ごとに光を計算する。
//   ライティングのパスの結果は、呼び出し側が持つ HDR の描画先（16 ビットの浮動小数点）へ書く。
//   HLSL の書き出しと読み出しは、EngineContent/Shaders/Deferred の GBufferWrite.hlsli / GBufferRead.hlsli（CDeferredShaders で文字列として取れる）。
//
//   // 作る（大きさが変わったら、もう一度 Create する。前の描画先は、参照が消えると解放される）
//   CGBuffer GBuffer;
//   GBuffer.Create( *Assets, "scene", 1280, 720 );
//
//   // 記録（FDrawContext の中。物は、ピクセルシェーダーが PackGBuffer で 4 つの出力を書く）
//   GBuffer.BeginGeometryPass( CommandList );
//   ... UseVertexShader / UsePixelShader、PrimitiveIndexedShader3D ...
//   GBuffer.EndGeometryPass( CommandList );
//
//   // ライティング: 画像の段 0〜3 に G-buffer を置いて、画面全体の四角形を、ライティングのピクセルシェーダーで描く
//   CommandList.PushTarget( HdrColor );
//   GBuffer.UseForLighting( CommandList );
//   FGBufferView View;
//   FGBufferView::Make( Camera, Viewport, View );
//   CommandList.ShaderConstants( EShaderStage::Pixel, CGBuffer::ViewConstantSlot, View );
//   CommandList.UsePixelShader( Lighting );
//   CommandList.ShaderRect( 0.0f, 0.0f, Width, Height );
//   CommandList.PopTarget();
//=============================================================================
class CGBuffer
{
public:
	// FGBufferView を置く定数バッファのスロット（HLSL の cbGBufferView : register( b6 ) と同じ）
	static constexpr int32 ViewConstantSlot = 6;
	// G-buffer の枚数と、ライティングのパスで画像を置く段の数
	static constexpr int32 NumTargets = 4;

	// 描画先の作り方（Target ごとの形式・チャンネル数。Albedo だけ奥行きバッファを持つ）
	static FRenderTargetDesc MakeDesc( EGBufferTarget Target, int32 Width, int32 Height );

	// G-buffer を作る。Name には、枚数ぶんの接尾辞を付ける。作り直すときは、前の描画先を手放す。1 枚でも作れなければ、何も持たず false
	bool Create( CAssetSubsystem& Assets, const FString& Name, int32 Width, int32 Height );

	// 自分で作った 4 枚（Albedo / Normal / Depth / Emission の順）を受け取る。4 枚とも同じ大きさで、MakeDesc と同じ形式・チャンネル数・奥行きバッファのときだけ受け取る
	// （違えば、持っているものは何も変えず false）。Create も、作った 4 枚をここで確かめる
	bool Adopt( const TSharedPtr<CRenderTarget> ( &InTargets )[NumTargets] );

	// 描画先を手放す
	void Release();

	// 4 枚とも作れているか
	bool IsValid() const;

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }

	// 1 枚の描画先（作れていなければ、空の参照）
	const TSharedPtr<CRenderTarget>& GetTarget( EGBufferTarget Target ) const;

	// ジオメトリのパスの始まり: 4 枚を消し（地の色・自分で光る色は黒、法線は 0、深度は 1 = 何も描かれていない）、
	// 地の色を描く先にして、法線・深度・自分で光る色を、出力 1〜3（SV_Target1〜3）へ置く。作れていなければ、何も記録せず false
	bool BeginGeometryPass( FDrawContext& CommandList ) const;

	// ジオメトリのパスの終わり: 出力 1〜3 を外し、シェーダーを外し、描く先を戻す（このあと、4 枚を画像として読める）
	void EndGeometryPass( FDrawContext& CommandList ) const;

	// ライティングのために、4 枚を、画像の段 0〜3（Albedo / Normal / Depth / Emission の順）に置く。値を正確に読むため、貼り方は最近傍・端は伸ばす設定にする。作れていなければ false
	bool UseForLighting( FDrawContext& CommandList ) const;

private:
	TSharedPtr<CRenderTarget> Targets[NumTargets];
	int32 Width = 0;
	int32 Height = 0;
};
