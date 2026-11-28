// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

class IRenderSurface;
class ITextureDrawSource;
class IGeometryBufferSource;
class IStaticModelSource;
class IFontDrawSource;
class IRenderTargetDrawSource;
class IShaderDrawSource;

// DX12の描画先を共通のキャンバス命令実行側へ接続する。
class CDX12DrawExecutorSubsystem : public CAppSubsystem, public IDrawExecutor
{
public:
	// --backend=dx12のときだけ作る。
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// 描画先と画像窓口を先に初期化する。
	void Initialize( FSubsystemCollection& Collection ) override;
	// 借用先を外す。
	void Deinitialize() override;
	// フレーム内の命令を共通実行側へ渡す。
	void Execute( const FDrawSpan& Span ) override;
	// 以後のExecuteを、指定寸法と倍率の撮影先へ向ける。通常のフレーム外・二重開始はfalse。
	bool BeginOffscreenCapture( const FOffscreenCaptureDesc& Desc ) override;
	// 撮影先の画素を読み、通常描画へ戻す。失敗時はOutを保つ。
	bool EndOffscreenCapture( TArray<FColor32>& Out ) override;
	// 描くときだけ骨を上書きする命令（ModelFramePose3D）に対応する。
	bool SupportsModelFrameOverrides() const override { return true; }
	// 材質付きの3D四角形・帯（消失・境界の発光・UVのゆがみ）に対応する。
	bool SupportsEffectMaterials() const override { return true; }
	bool SupportsEffectMaterialRibbons() const override { return true; }
	bool SupportsEffectUVDistortion() const override { return true; }
	bool SupportsEffectRibbonUVDistortion() const override { return true; }
	bool SupportsEffectMaterialMesh() const override { return true; }
	// モデルの一材質だけに材質効果をかける（ModelMaterial3D）。
	bool SupportsModelMaterialEffects() const override { return true; }
private:
	bool bSolidShapeAlphaSquared = true; // 見た目の水準が compat（DxLib に合わせる）なら、立体図形の半透明をアルファの 2 乗で描く。
	IRenderSurface* Surface = nullptr; // 借用する描画先。
	IStaticModelSource* Models = nullptr; // 借用する静的モデル。
	IGeometryBufferSource* Buffers = nullptr; // 借用する登録形状。
	ITextureDrawSource* Textures = nullptr; // 借用する画像窓口。
	IFontDrawSource* Fonts = nullptr; // 借用するフォントの表。
	IRenderTargetDrawSource* Targets = nullptr; // 借用する描画先の表。
	IShaderDrawSource* Shaders = nullptr; // 借用するシェーダーの実行先。
};
