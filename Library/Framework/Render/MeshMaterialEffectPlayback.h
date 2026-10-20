// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_RENDER_MESHMATERIALEFFECTPLAYBACK_H
#define FRAMEWORK_RENDER_MESHMATERIALEFFECTPLAYBACK_H

#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"

// 材質効果一つ分の個体別再生状態を持ち、共有資源を持たずに時計を進める。
struct FMeshMaterialEffectPlayback
{
	// 再生位置に応じて描画を選ぶ状態。
	enum class EEffectPlaybackState : uint8
	{
		Stopped,  // 従来表示へ戻す状態。
		Playing,  // 時間とUVを進める状態。
		Paused,   // 現在の表示を保って時計を止めた状態。
		Finished, // 非loop効果の最終表示を保つ状態。
	};

	// 現在の再生状態。
	EEffectPlaybackState EffectState = EEffectPlaybackState::Stopped;
	// 曲線評価に使うDuration内の秒数。
	float64 EffectTimeSeconds = 0.0;
	// 元画像UVの秒単位scrollを1周期内へ畳んだ値。
	float64 EffectUVOffset[2] = {};
	// ゆがみ画像UVの秒単位scrollを1周期内へ畳んだ値。
	float64 DistortionUVOffset[2] = {};

	// 有効な再生中だけ有限正数のdeltaを加え、曲線時間と二組のUV位相を更新する。
	void Update( float32 DeltaTime, const FMeshMaterialEffectDefinition& Definition );
	// 停止または終了後は最初から始め、pause中なら位置を保持して再開する。
	void Play();
	// 再生中だけ現在位置を保って一時停止する。
	void Pause();
	// 再生位置と二組のUV位相を0へ戻して停止する。
	void Stop();
	// 再生位置と二組のUV位相を0へ戻して再生する。
	void Restart();
};

#endif
