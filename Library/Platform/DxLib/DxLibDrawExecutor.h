#pragma once
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Platform/DxLib/EffectMaterialRenderer.h"

//=============================================================================
// CDxLibDrawExecutor
//   IDrawExecutor の DxLib 実装。描画システムが（並列に）記録した描画コマンドを、メインスレッドで、記録された順に DxLib の
//   描画関数へ流す。アプリで 1 つ、自動で生成される。
//
//   DxLib はスレッドセーフではない（DX_THREAD_SAFE が無効で、描画先・カメラ・ブレンドモードなどが全体で 1 つ）ので、
//   DxLib を呼ぶのはここ（メインスレッド）だけ。記録する側は DxLib に一切触れない。
//
//   コマンドと DxLib の対応:
//     Box -> DrawBox / Circle -> DrawCircle / Line -> DrawLine / Text -> DrawString / Sprite -> DrawRotaGraphF
//     Blend -> SetDrawBlendMode
//     Line3D -> DrawLine3D / Triangle3D -> DrawTriangle3D / Sphere3D -> DrawSphere3D / Capsule3D -> DrawCapsule3D
//     Cube3D -> DrawCube3D / Billboard3D -> DrawBillboard3D / WorldMatrix -> SetTransformToWorld
//     Model3D -> MV1SetMatrix + (MV1SetAttachAnimTime) + MV1DrawModel
//   Span の終わりで、ブレンドモードはブレンドなしに、WorldMatrix は単位行列に、Tint（SetDrawBright）は白に戻す。
//
//   DxLib の DrawBox などは、頂点を内部でためてまとめて描く作りなので、こちらで頂点配列にまとめ直さない
//   （DrawPrimitive2D でまとめると、10 万個の Box で DrawBox の約 8 倍遅かった。バッチの大きさを変えても同じ）。
//=============================================================================
class CDxLibDrawExecutor : public CAppSubsystem, public IDrawExecutor
{
public:
	// DxLibと材質描画用GPU資源を初期化する。
	void Initialize( FSubsystemCollection& Collection ) override;
	// DxLib終了前に材質描画用GPU資源を解放する。
	void Deinitialize() override;

	// 記録済み描画命令を順番に実行する。
	void Execute( const FDrawSpan& Span ) override;
	// 現在のDirect3D環境で材質シェーダーを使えるか返す。
	bool SupportsEffectMaterials() const override;
	// 現在のDirect3D環境で材質付き帯を描画できるか返す。
	bool SupportsEffectMaterialRibbons() const override;
	// 現在のDirect3D環境で材質UVのゆがみを描画できるか返す。
	bool SupportsEffectUVDistortion() const override;
	// 現在のDirect3D環境で材質帯のUVゆがみを描画できるか返す。
	bool SupportsEffectRibbonUVDistortion() const override;

private:
	// DxLibが動作中に解放する必要がある材質描画用GPU資源。
	FEffectMaterialRenderer EffectMaterialRenderer;
};
