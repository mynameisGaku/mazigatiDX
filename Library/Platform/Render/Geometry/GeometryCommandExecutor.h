// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Platform/Render/Geometry/VertexLighting.h"

// カメラ・ワールド・深度を共通命令から解釈する。GPUやImGuiには依存しない。
class CGeometryCommandExecutor
{
public:
	// 呼び出した命令を扱ったか。未対応・不正はSpanを中止するため区別する。
	enum class EResult : uint8
	{
		NotHandled, // この実行側の命令ではない。
		Executed, // 状態を更新または描画を記録した。
		Rejected, // 不正・未対応・GPU準備の失敗。
	};
	// Geometryは借用。nullptrでも状態命令は読めるが、描画は失敗する。
	explicit CGeometryCommandExecutor( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures = nullptr, IGeometryBufferSource* InBuffers = nullptr, IStaticModelSource* InModels = nullptr );
	// 影の描画に使う、シェーダーの実行先（nullptrなら、影の絵を描けず、影も受けられない）。
	void SetShaderSource( IShaderDrawSource* InSource ) { ShaderSource = InSource; }
	// 影の絵を描く間だけ呼ぶ。以降の3D図形は、画面でなく、光から見た深度として、いまの描画先（影の絵）へ描く。
	void BeginShadowCasting( const FMatrix4x4& InLightViewProjection )
	{
		bShadowCasting = true;
		LightViewProjection = InLightViewProjection;
	}
	void EndShadowCasting() { bShadowCasting = false; }
	// 影を受ける: スロット Slot（0〜2）に、影の絵 ShadowMap を使う。ShadowMap が負なら、そのスロットを外す。使うのは、使っているスロットのうち、いちばん小さいもの。
	void SetShadowReceiver( int32 Slot, int32 ShadowMap );
	// いまのカメラ。
	const FCamera& GetCamera() const { return Camera; }
	// いまの3Dの実行先（描画先）。
	IRenderGeometry3D* GetGeometry() const { return Geometry; }
	// いまの深度・表裏などの状態。
	const FGeometryDrawState& GetState() const { return State; }
	// ShaderQuad3Dの四角形を、クリップ座標つきの頂点（三角形のリスト）にする。カメラ・ワールド行列・向きの決め方は、Quad3Dと同じ。失敗はfalse（Outを保つ）。
	bool BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out );
	// 描画先が替わったとき、3Dの実行先を差し替える（状態・カメラ・ライトは保つ）。nullptrなら、描画は失敗する。
	void SetGeometry( IRenderGeometry3D* InGeometry ) { Geometry = InGeometry; }
	// 以降の描画の、描画先との混ぜ方（合成するときだけ使う）。
	void SetBlend( EGeometryBlend Blend ) { State.Blend = Blend; }
	// 以降の描画の、利用者が決めた混ぜ方の係数（Blend が Custom のときに使う）。
	void SetCustomBlend( const FGeometryCustomBlend& Blend ) { State.CustomBlend = Blend; }
	// 1命令を検査して実行する。Stateはこのインスタンス内だけに閉じる。
	EResult Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
private:
	// 影の絵を描いているとき、図形を光から見た深度として描く。影を落とさない図形（線・点・画像つき・半透明）は、何もせず成功。
	bool SubmitShadowCaster( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, int32 Alpha, int32 Texture );
	// ワールド座標を同次クリップ座標にして記録する。全入力を検査してからGPUへ渡す。
	// bLit なら、頂点の法線と現在のライティングから、頂点ごとに色を計算する（光の計算を切っているときは、拡散色＋鏡面色）。
	bool Submit( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, int32 Texture = -1, bool bLit = false );
	// 両面・画像の透明度を一命令だけ適用する。カリングは失敗時も戻す。
	bool SubmitQuad( const TArray<FDrawVertex3D>& Vertices, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bLit = false );
	// indexを解決した点列をリストへ展開する。GPU・画像に依存しない。
	bool SubmitPrimitive( const TArray<FDrawVertex3D>& Ordered, EDrawPrimitive Type, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	ITextureDrawSource* Textures; // 登録済み画像を同期実行中だけ借りる。
	IStaticModelSource* Models; // 同期実行中だけ借りる静的モデル。
	IGeometryBufferSource* Buffers; // 登録済みのローカル形状を借りる。
	IRenderGeometry3D* Geometry; // 借用するGPU実行先。
	FMatrix4x4 World; // 現在のワールド行列。
	FCamera Camera; // 現在のカメラ。
	FGeometryDrawState State; // 現在の深度と表裏。
	CVertexLighting Lighting; // このSpanだけのライティング（光）の状態。
	FDrawFogData Fog; // このSpanだけの距離フォグ（Modeが0なら、なし）。
	IShaderDrawSource* ShaderSource = nullptr; // 影の描画に使うシェーダーの実行先（借用）。
	bool bShadowCasting = false; // 影の絵を描いているか。
	FMatrix4x4 LightViewProjection; // 影の絵を描くときの、光から見た行列。
	int32 ShadowSlotMaps[3] = { -1, -1, -1 }; // スロットごとの、使っている影の絵。
	int32 ReceiverSlot = -1; // 影を受けるスロット（-1 は、受けない）。
	TArray<FMatrix4x4> WorldStack; // このSpanだけの変換の積み重ね。
	TArray<FCamera> CameraStack; // このSpanだけのカメラの積み重ね。
};
