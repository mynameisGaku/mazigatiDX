// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Platform/Render/Geometry/GeometryExecution.h"
#include "Library/Platform/Render/Geometry/GeometryForwardMesh.h"
#include "Library/Platform/Render/Geometry/GeometryModelCommands.h"
#include "Library/Platform/Render/Geometry/GeometryShapeCommands.h"
#include "Library/Platform/Render/Geometry/GeometryStateCommands.h"
#include "Library/Platform/Render/Geometry/GeometryVertexSubmitter.h"
#include "Library/Platform/Render/Geometry/VertexLighting.h"

// カメラ・ワールド・深度を共通命令から解釈する。GPUやImGuiには依存しない。
// 命令の系統ごとの役割（状態・図形・静的モデル）と、頂点の提出・GPUで描く共有メッシュの役割へ、振り分ける。状態は、このSpanだけの FGeometryExecution に集める。
class CGeometryCommandExecutor
{
public:
	// 呼び出した命令を扱ったか。未対応・不正はSpanを中止するため区別する。
	using EResult = EGeometryCommandResult;
	// Geometryは借用。nullptrでも状態命令は読めるが、描画は失敗する。
	explicit CGeometryCommandExecutor( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures = nullptr, IGeometryBufferSource* InBuffers = nullptr, IStaticModelSource* InModels = nullptr );
	// 役割が状態を借りているので、複写・移動しない。
	CGeometryCommandExecutor( const CGeometryCommandExecutor& ) = delete;
	CGeometryCommandExecutor& operator=( const CGeometryCommandExecutor& ) = delete;
	// 影の描画に使う、シェーダーの実行先（nullptrなら、影の絵を描けず、影も受けられない）。
	void SetShaderSource( IShaderDrawSource* InSource ) { Run.ShaderSource = InSource; }
	// 影の絵を描く間だけ呼ぶ。以降の3D図形は、画面でなく、光から見た深度として、いまの描画先（影の絵）へ描く。
	void BeginShadowCasting( const FMatrix4x4& InLightViewProjection )
	{
		Run.bShadowCasting = true;
		Run.LightViewProjection = InLightViewProjection;
	}
	void EndShadowCasting() { Run.bShadowCasting = false; }
	// 影を受ける: スロット Slot（0〜2）に、影の絵 ShadowMap を使う。ShadowMap が負なら、そのスロットを外す。使うのは、使っているスロットのうち、いちばん小さいもの。
	void SetShadowReceiver( int32 Slot, int32 ShadowMap );
	// いまのカメラ。
	const FCamera& GetCamera() const { return Run.Camera; }
	// いまの3Dの実行先（描画先）。
	IRenderGeometry3D* GetGeometry() const { return Run.Geometry; }
	// いまの深度・表裏などの状態。
	const FGeometryDrawState& GetState() const { return Run.State; }
	// ShaderQuad3Dの四角形を、クリップ座標つきの頂点（三角形のリスト）にする。カメラ・ワールド行列・向きの決め方は、Quad3Dと同じ。失敗はfalse（Outを保つ）。
	bool BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out );
	// 描画先が替わったとき、3Dの実行先を差し替える（状態・カメラ・ライトは保つ）。nullptrなら、描画は失敗する。
	void SetGeometry( IRenderGeometry3D* InGeometry ) { Run.Geometry = InGeometry; }
	// 以降の描画の、描画先との混ぜ方（合成するときだけ使う）。
	void SetBlend( EGeometryBlend Blend ) { Run.State.Blend = Blend; }
	// 以降の描画の、利用者が決めた混ぜ方の係数（Blend が Custom のときに使う）。
	void SetCustomBlend( const FGeometryCustomBlend& Blend ) { Run.State.CustomBlend = Blend; }
	// 1命令を検査して実行する。Stateはこのインスタンス内だけに閉じる。
	EResult Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// 静的モデルを 1 つ描く（アニメーションの時間と配置を設定してから、いまの姿で描く）。Alpha・bAlphaBlend は、Execute へ渡された値。
	EResult DrawModel( const FDrawModelData& Data, int32 Alpha, bool bAlphaBlend );
	// 現在の配置で固定姿勢を描く。モデル登録へのアクセスや更新は行わない。
	EResult DrawModelView( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend );
	// GPU が変換と光を計算する経路（共有のメッシュを GPU に置く）を、使ってよいか（影・フォグ・画素ごとの光などは、CPU の経路で描く）。
	void SetForwardPath( bool bEnable ) { Run.bUseForwardPath = bEnable; }
	// 画像番号から描画用の矩形を取得する窓口（無ければ nullptr）。
	ITextureDrawSource* GetTextures() const { return Run.Textures; }
	// 借用している静的モデルの窓口（無ければ nullptr）。
	IStaticModelSource* GetModels() const { return Run.Models; }

private:
	FGeometryExecution Run; // このSpanだけの3Dの状態と、借りた実行先。役割が共有する。
	CGeometryForwardMesh Forward; // GPU が変換と光を計算する共有メッシュの経路。
	CGeometryVertexSubmitter Submitter; // 頂点の提出（ワールドからクリップ座標・光・フォグ・影）。
	CGeometryStateCommands StateCommands; // カメラ・ワールド・深度・ライト・フォグなどの状態の命令。
	CGeometryShapeCommands ShapeCommands; // 3Dの図形の命令。
	CGeometryModelCommands ModelCommands; // 静的モデル・メッシュの命令。
};
