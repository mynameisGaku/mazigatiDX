// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

class IRenderSurface;
class ITextureDrawSource;
class IGeometryBufferSource;
class IStaticModelSource;

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
private:
	IRenderSurface* Surface = nullptr; // 借用する描画先。
	IStaticModelSource* Models = nullptr; // 借用する静的モデル。
	IGeometryBufferSource* Buffers = nullptr; // 借用する登録形状。
	ITextureDrawSource* Textures = nullptr; // 借用する画像窓口。
};
