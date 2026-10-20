// SPDX-License-Identifier: NOASSERTION
#ifndef PARTICLE_EFFECT_EDITING_H
#define PARTICLE_EFFECT_EDITING_H

#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Effect/ParticleEffect.h"

// 粒子の定義と、保存状態・元に戻す履歴を持つ編集用文書。
using TParticleEffectDocument = TDefinitionDocument<FParticleEffectDefinition>;

namespace NParticleEdit
{
	// 名前が重ならないエミッターを末尾に追加する。上限に達していれば INDEX_NONE。
	int32 AddEmitter( TParticleEffectDocument& Document, const FString& Name = "Emitter" );
	// 指定位置を複製して直後へ挿入する。範囲外または上限到達なら INDEX_NONE。
	int32 DuplicateEmitter( TParticleEffectDocument& Document, int32 Index );
	// 指定位置を削除する。範囲外なら変更せず false。
	bool RemoveEmitter( TParticleEffectDocument& Document, int32 Index );
	// 指定位置を移し、移動先の番号を To にする。範囲外または同じ位置なら false。
	bool MoveEmitter( TParticleEffectDocument& Document, int32 From, int32 To );
	// 指定位置の全項目を入れ替え、一括生成を時刻順にする。範囲外または同値なら false。Key は連続編集をまとめる識別名。
	bool SetEmitter( TParticleEffectDocument& Document, int32 Index, const FParticleEmitterDefinition& Emitter, const char* Key = nullptr );
	// 定義全体の名前・継続秒・繰り返し・シードを変える。同値なら false。Key は連続編集をまとめる識別名。
	bool SetProperties( TParticleEffectDocument& Document, const FString& Name, float32 Duration, bool bLoop, uint32 Seed, const char* Key = nullptr );
}

#endif
