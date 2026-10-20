// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_EFFECTEDITORCURVEINPUT_H
#define FRAMEWORK_DEBUGUI_EFFECTEDITORCURVEINPUT_H

#include "Library/Framework/Effect/EffectColorCurve.h"
#include "Library/Framework/Effect/EffectScalarCurve.h"
#include "Library/Framework/Effect/ParticleEffectEditing.h"

namespace NEffectEditorUI
{
	// 保存済みのscalar curveを編集し、入力終了時に履歴sessionを区切る。
	bool DrawScalarCurve( const char* Label, FEffectScalarCurve& Curve, TParticleEffectDocument& Document, float32 DefaultValue, float32 Minimum, float32 Maximum );
	// 文書へ依存せずscalar curveを編集し、変更と入力終了を呼出側へ返す。
	bool DrawScalarCurve( const char* Label, FEffectScalarCurve& Curve, float32 DefaultValue, float32 Minimum, float32 Maximum, bool& bInputEnded );
	// RGBA曲線を編集し、不正なキーは入力へ渡さず削除か初期化で直せるようにする。
	bool DrawColorCurve( const char* Label, FEffectColorCurve& Curve, TParticleEffectDocument& Document );
}

#endif
