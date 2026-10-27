// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_RENDER_MODELMATERIALEFFECTRENDERER_H
#define FRAMEWORK_RENDER_MODELMATERIALEFFECTRENDERER_H


#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Framework/Render/MeshMaterialEffectPlayback.h"
#include "Library/Math/Matrix4x4.h"

class CModel;
class CModelInstance;
class FDrawContext;
class IDrawExecutor;
struct FModelMaterialEffectResources;

// Modelの選択材質だけを描く共有資源と、個体別の再生時計を持つ値Renderer。
struct FModelMaterialEffectRenderer : FMeshMaterialEffectPlayback
{
	// 元ModelのMaterialIndexへDefinitionを準備する。対応executor・main thread・ready model・必要画像が無ければfalse。
	bool Prepare( const TSharedPtr<CModel>& Source, int32 MaterialIndex, const FMeshMaterialEffectDefinition& Definition, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const IDrawExecutor& Executor, bool bTwoSided = false );
	// 効果資源を解放してnative Model描画へ戻す。
	void Clear();
	// 準備済み資源があれば個体時計を進める。
	void Update( float32 DeltaTime );
	// 停止後は先頭から、pause後は保持位置から再開する。
	void Play();
	// 再生中の表示を保って時計を止める。
	void Pause();
	// 時刻0へ戻しnative Model描画へ戻す。
	void Stop();
	// 時刻0から効果描画を開始する。
	void Restart();
	// 準備済みかを返す。
	bool IsPrepared() const;
	// 現在の個体別再生状態を返す。
	EEffectPlaybackState GetPlaybackState() const;
	// effect curveに使う現在秒数を返す。
	float64 GetEffectTime() const;
	// InstanceとWorldの有効再生を引き受けてtrue、native fallback時はfalseを返す。不正行列はtrueで無記録にする。
	bool Record( FDrawContext& Draw, const TSharedPtr<CModelInstance>& Instance, const FMatrix4x4& World ) const;

private:
	// 複製間で共有する変更不可の資源。
	TSharedPtr<const FModelMaterialEffectResources> Resources;
};

#endif
